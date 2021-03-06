/******************************************************************************
 *
 * Copyright(c) 2016 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8821C_OPS_C_

#include <drv_types.h>		/* basic_types.h, rtw_io.h and etc. */
#include <rtw_xmit.h>		/* struct xmit_priv */
#ifdef DBG_CONFIG_ERROR_DETECT
	#include <rtw_sreset.h>
#endif /* DBG_CONFIG_ERROR_DETECT */
#include <hal_data.h>		/* PHAL_DATA_TYPE, GET_HAL_DATA() */
#include <hal_com.h>		/* rtw_hal_config_rftype(), dump_chip_info() and etc. */
#include "../hal_halmac.h"	/* GET_RX_DESC_XXX_8821C() */
#include "rtl8821c.h"
#ifdef CONFIG_PCI_HCI
	#include "rtl8821ce_hal.h"
#endif
#include "rtl8821c_dm.h"


static void read_chip_version(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u32 value32;


	hal = GET_HAL_DATA(adapter);

	value32 = rtw_read32(adapter, REG_SYS_CFG1_8821C);
	hal->version_id.ICType = CHIP_8821C;
	hal->version_id.ChipType = ((value32 & BIT_RTL_ID_8821C) ? TEST_CHIP : NORMAL_CHIP);
	hal->version_id.CUTVersion = BIT_GET_CHIP_VER_8821C(value32);
	hal->version_id.VendorType = BIT_GET_VENDOR_ID_8821C(value32);
	hal->version_id.VendorType >>= 2;
	switch (hal->version_id.VendorType) {
	case 0:
		hal->version_id.VendorType = CHIP_VENDOR_TSMC;
		break;
	case 1:
		hal->version_id.VendorType = CHIP_VENDOR_SMIC;
		break;
	case 2:
		hal->version_id.VendorType = CHIP_VENDOR_UMC;
		break;
	}
	hal->version_id.RFType = ((value32 & BIT_RF_TYPE_ID_8821C) ? RF_TYPE_2T2R : RF_TYPE_1T1R);
	hal->RegulatorMode = ((value32 & BIT_SPSLDO_SEL_8821C) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	value32 = rtw_read32(adapter, REG_SYS_STATUS1_8821C);
	hal->version_id.ROMVer = BIT_GET_RF_RL_ID_8821C(value32);

	/* For multi-function consideration. */
	hal->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(adapter, REG_WL_BT_PWR_CTRL_8821C);
	hal->MultiFunc |= ((value32 & BIT_WL_FUNC_EN_8821C) ? RT_MULTI_FUNC_WIFI : 0);
	hal->MultiFunc |= ((value32 & BIT_BT_FUNC_EN_8821C) ? RT_MULTI_FUNC_BT : 0);
	hal->PolarityCtl = ((value32 & BIT_WL_HWPDN_SL_8821C) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);

	rtw_hal_config_rftype(adapter);

	dump_chip_info(hal->version_id);
}

/*
 * Return:
 *	_TRUE	valid ID
 *	_FALSE	invalid ID
 */
static u8 Hal_EfuseParseIDCode(PADAPTER adapter, u8 *map)
{
	u16 EEPROMId;


	/* Check 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*(u16 *)map);
	RTW_INFO("EEPROM ID = 0x%04x\n", EEPROMId);
	if (EEPROMId == RTL_EEPROM_ID)
		return _TRUE;

	RTW_INFO("<WARN> EEPROM ID is invalid!!\n");
	return _FALSE;
}

static void Hal_EfuseParseEEPROMVer(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (_TRUE == mapvalid)
		hal->EEPROMVersion = map[EEPROM_VERSION_8821C];
	else
		hal->EEPROMVersion = 1;

	RTW_INFO("EEPROM Version = %d\n", hal->EEPROMVersion);
}

static void Hal_EfuseParseTxPowerInfo(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	TxPowerInfo24G tbl2G4;
	TxPowerInfo5G tbl5g;

	hal_load_txpwr_info(adapter, &tbl2G4, &tbl5g, map);

	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8821C] != 0xFF))
		hal->EEPROMRegulatory = map[EEPROM_RF_BOARD_OPTION_8821C] & 0x7; /* bit0~2 */
	else
		hal->EEPROMRegulatory = EEPROM_DEFAULT_BOARD_OPTION & 0x7; /* bit0~2 */
	RTW_INFO("EEPROM Regulatory=0x%02x\n", hal->EEPROMRegulatory);
}

static void Hal_EfuseParseBoardType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8821C] != 0xFF))
		hal->InterfaceSel = (map[EEPROM_RF_BOARD_OPTION_8821C] & 0xE0) >> 5;
	else
		hal->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION & 0xE0) >> 5;

	RTW_INFO("EEPROM Board Type=0x%02x\n", hal->InterfaceSel);
}

static void Hal_EfuseParseBTCoexistInfo(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 setting;
	u32 tmpu4;

	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8821C] != 0xFF)) {
		/* 0xc1[7:5] = 0x01 */
		if (((map[EEPROM_RF_BOARD_OPTION_8821C] & 0xe0) >> 5) == 0x01)
			hal->EEPROMBluetoothCoexist = _TRUE;
		else
			hal->EEPROMBluetoothCoexist = _FALSE;
	} else
		hal->EEPROMBluetoothCoexist = _FALSE;

	hal->EEPROMBluetoothType = BT_RTL8821C;

	setting = map[EEPROM_RF_BT_SETTING_8821C];
	if ((_TRUE == mapvalid) && (setting != 0xFF)) {
		/*Bit[0]: Total antenna number
			0: 2-Antenna / 1: 1-Antenna	*/
		hal->EEPROMBluetoothAntNum = setting & BIT(0);
		/*
		 * Bit[6]: One-Ant structure use Ant2(aux.) path or Ant1(main) path
		 *	0: Ant2(aux.)
		 *	1: Ant1(main), default
		 */
		hal->ant_path = (setting & BIT(6)) ? ODM_RF_PATH_B : ODM_RF_PATH_A;
	} else {
		hal->EEPROMBluetoothAntNum = Ant_x1;
		hal->ant_path = ODM_RF_PATH_A;
	}

exit:
	RTW_INFO("EEPROM %s BT-coex, ant_num=%d\n",
		 hal->EEPROMBluetoothCoexist == _TRUE ? "Enable" : "Disable",
		 hal->EEPROMBluetoothAntNum == Ant_x2 ? 2 : 1);
}

static void Hal_EfuseParseChnlPlan(PADAPTER adapter, u8 *map, u8 autoloadfail)
{
	adapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
						adapter,
						map ? &map[EEPROM_COUNTRY_CODE_8821C] : NULL,
						map ? map[EEPROM_CHANNEL_PLAN_8821C] : 0xFF,
						adapter->registrypriv.alpha2,
						adapter->registrypriv.channel_plan,
						RTW_CHPLAN_REALTEK_DEFINE,
						autoloadfail
					);

	RTW_INFO("EEPROM ChannelPlan=0x%02x\n", adapter->mlmepriv.ChannelPlan);
}

static void Hal_EfuseParseXtal(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if ((_TRUE == mapvalid) && map[EEPROM_XTAL_8821C] != 0xFF)
		hal->crystal_cap = map[EEPROM_XTAL_8821C];
	else
		hal->crystal_cap = EEPROM_Default_CrystalCap;

	RTW_INFO("EEPROM crystal_cap=0x%02x\n", hal->crystal_cap);
}

static void Hal_EfuseParseThermalMeter(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/* ThermalMeter from EEPROM */
	if ((_TRUE == mapvalid) && (map[EEPROM_THERMAL_METER_8821C] != 0xFF))
		hal->eeprom_thermal_meter = map[EEPROM_THERMAL_METER_8821C];
	else {
		hal->eeprom_thermal_meter = EEPROM_Default_ThermalMeter;
		hal->odmpriv.rf_calibrate_info.is_apk_thermal_meter_ignore = _TRUE;
	}

	RTW_INFO("EEPROM ThermalMeter=0x%02x\n", hal->eeprom_thermal_meter);
}

static void Hal_EfuseParseAntennaDiversity(PADAPTER adapter, u8 *map, u8 mapvalid)
{
#ifdef CONFIG_ANTENNA_DIVERSITY
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct registry_priv *registry_par = &adapter->registrypriv;


	if (hal->EEPROMBluetoothAntNum == Ant_x1)
		hal->AntDivCfg = 0;
	else {
		if (registry_par->antdiv_cfg == 2)/* 0:OFF , 1:ON, 2:By EFUSE */
			hal->AntDivCfg = (map[EEPROM_RF_BOARD_OPTION_8821C] & BIT3) ? _TRUE : _FALSE;
		else
			hal->AntDivCfg = registry_par->antdiv_cfg;
	}
	/*hal->TRxAntDivType = S0S1_TRX_HW_ANTDIV;*/
	hal->with_extenal_ant_switch = ((map[EEPROM_RF_BT_SETTING_8821C] & BIT7) >> 7);

	RTW_INFO("EEPROM AntDivCfg=%d, AntDivType=%d, extenal_ant_switch:%d\n",
		 __func__, hal->AntDivCfg, hal->TRxAntDivType, hal->with_extenal_ant_switch);
#endif /* CONFIG_ANTENNA_DIVERSITY */
}

static void Hal_EfuseTxBBSwing(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);

	if (_TRUE == mapvalid) {
		hal_data->tx_bbswing_24G = map[EEPROM_TX_BBSWING_2G_8821C];
		if (0xFF == hal_data->tx_bbswing_24G)
			hal_data->tx_bbswing_24G = 0;
		hal_data->tx_bbswing_5G = map[EEPROM_TX_BBSWING_5G_8821C];
		if (0xFF == hal_data->tx_bbswing_5G)
			hal_data->tx_bbswing_5G = 0;
	} else {
		hal_data->tx_bbswing_24G = 0;
		hal_data->tx_bbswing_5G = 0;
	}
	RTW_INFO("EEPROM tx_bbswing_24G =0x%02x\n", hal_data->tx_bbswing_24G);
	RTW_INFO("EEPROM tx_bbswing_5G =0x%02x\n", hal_data->tx_bbswing_5G);
}

static void Hal_EfuseParseCustomerID(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (_TRUE == mapvalid)
		hal->EEPROMCustomerID = map[EEPROM_CUSTOMER_ID_8821C];
	else
		hal->EEPROMCustomerID = 0;
	RTW_INFO("EEPROM Customer ID=0x%02x\n", hal->EEPROMCustomerID);
}

static void Hal_DetectWoWMode(PADAPTER adapter)
{
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	adapter_to_pwrctl(adapter)->bSupportRemoteWakeup = _TRUE;
#else /* !(CONFIG_WOWLAN || CONFIG_AP_WOWLAN) */
	adapter_to_pwrctl(adapter)->bSupportRemoteWakeup = _FALSE;
#endif /* !(CONFIG_WOWLAN || CONFIG_AP_WOWLAN) */

	RTW_INFO("EEPROM SupportRemoteWakeup=%d\n", adapter_to_pwrctl(adapter)->bSupportRemoteWakeup);
}

static void hal_ReadPAType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);

	if (mapvalid) {
		/* AUTO - Get INFO from eFuse*/
		if (GetRegAmplifierType2G(adapter) == 0) {
			switch (hal_data->rfe_type) {
			default:
					hal_data->PAType_2G = 0;
					hal_data->LNAType_2G = 0;
					hal_data->ExternalPA_2G = 0;
					hal_data->ExternalLNA_2G = 0;
				break;
			}
		} else {
			hal_data->ExternalPA_2G  = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			hal_data->ExternalLNA_2G = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		/* AUTO */
		if (GetRegAmplifierType5G(adapter) == 0) {
			switch (hal_data->rfe_type) {
			default:
				hal_data->PAType_5G = 0;
				hal_data->LNAType_5G = 0;
				hal_data->external_pa_5g = 0;
				hal_data->external_lna_5g = 0;
				break;
			}
		} else {
			hal_data->external_pa_5g  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal_data->external_lna_5g = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	} else {
		/*Get INFO from registry*/
		hal_data->ExternalPA_2G  = EEPROM_Default_PAType;
		hal_data->external_pa_5g  = 0xFF;
		hal_data->ExternalLNA_2G = EEPROM_Default_LNAType;
		hal_data->external_lna_5g = 0xFF;

		if (GetRegAmplifierType2G(adapter) == 0) {
			hal_data->ExternalPA_2G  = 0;
			hal_data->ExternalLNA_2G = 0;
		} else {
			hal_data->ExternalPA_2G  = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			hal_data->ExternalLNA_2G = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		if (GetRegAmplifierType5G(adapter) == 0) {
			hal_data->external_pa_5g  = 0;
			hal_data->external_lna_5g = 0;
		} else {
			hal_data->external_pa_5g  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal_data->external_lna_5g = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	}

	RTW_INFO("EEPROM PAType_2G is 0x%x, ExternalPA_2G = %d\n", hal_data->PAType_2G, hal_data->ExternalPA_2G);
	RTW_INFO("EEPROM PAType_5G is 0x%x, external_pa_5g = %d\n", hal_data->PAType_5G, hal_data->external_pa_5g);
	RTW_INFO("EEPROM LNAType_2G is 0x%x, ExternalLNA_2G = %d\n", hal_data->LNAType_2G, hal_data->ExternalLNA_2G);
	RTW_INFO("EEPROM LNAType_5G is 0x%x, external_lna_5g = %d\n", hal_data->LNAType_5G, hal_data->external_lna_5g);
}

static void Hal_ReadAmplifierType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);

	if (hal_data->rfe_type < 8) { /*According to RF-EFUSE DOC : R15]*/
		RTW_INFO("WIFI Module is iPA/iLNA\n");
		return;
	}

	hal_ReadPAType(adapter, map, mapvalid);

	/* [2.4G] extPA */
	hal_data->TypeGPA  = hal_data->PAType_2G;

	/* [5G] extPA */
	hal_data->TypeAPA  = hal_data->PAType_5G;

	/* [2.4G] extLNA */
	hal_data->TypeGLNA = hal_data->LNAType_2G;

	/* [5G] extLNA */
	hal_data->TypeALNA = hal_data->LNAType_5G;

	RTW_INFO("EEPROM TypeGPA = 0x%X\n", hal_data->TypeGPA);
	RTW_INFO("EEPROM TypeAPA = 0x%X\n", hal_data->TypeAPA);
	RTW_INFO("EEPROM TypeGLNA = 0x%X\n", hal_data->TypeGLNA);
	RTW_INFO("EEPROM TypeALNA = 0x%X\n", hal_data->TypeALNA);
}

static void Hal_ReadRFEType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	/* [20160-06-22 -R15]
		Type 0 - (2-Ant, DPDT), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 1 - (1-Ant, SPDT@Ant1), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 2 -(1-Ant, SPDT@Ant1) , (2G_BTG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 3 - (1-Ant, DPDT@Ant2), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 4 - (1-Ant, DPDT@Ant2), (2G_BTG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 5 - (2-Ant), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 6 - (2-Ant), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 7 - (1-Ant), (2G_BTG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
	*/

	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	/* check registry valye */
	if (GetRegRFEType(adapter) != CONFIG_RTW_RFE_TYPE) {
		hal->rfe_type = GetRegRFEType(adapter);
		goto exit;
	}

	if (mapvalid) {
		/* check efuse map */
		hal->rfe_type = map[EEPROM_RFE_OPTION_8821C];
		if (0xFF != hal->rfe_type)
			goto exit;
	}

	/* error handle */
	hal->rfe_type = 0;
	RTW_ERR("please pg efuse or change RFE_Type of registrypriv!!\n");

exit:
	RTW_INFO("EEPROM rfe_type=0x%x\n", hal->rfe_type);
}


static void Hal_EfuseParsePackageType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
}

#ifdef CONFIG_USB_HCI
static void Hal_ReadUsbModeSwitch(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (_TRUE == mapvalid)
		/* check efuse 0x06 bit7 */
		hal->EEPROMUsbSwitch = (map[EEPROM_USB_MODE_8821CU] & BIT7) >> 7;
	else
		hal->EEPROMUsbSwitch = _FALSE;

	RTW_INFO("EEPROM USB Switch=%d\n", hal->EEPROMUsbSwitch);
}
#endif /* CONFIG_USB_HCI */

/*
 * Description:
 *	Collect all information from efuse or files.
 *	This function will do
 *	1. Read registers to check hardware efuse available or not
 *	2. Read Efuse/EEPROM
 *	3. Read file if necessary
 *	4. Parsing Efuse data
 */
u8 rtl8821c_read_efuse(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u8 val8;
	u8 *efuse_map = NULL;
	u8 valid;

	hal = GET_HAL_DATA(adapter);
	efuse_map = hal->efuse_eeprom_data;

	/* 1. Read registers to check hardware eFuse available or not */
	val8 = rtw_read8(adapter, REG_SYS_EEPROM_CTRL_8821C);
	hal->bautoload_fail_flag = (val8 & BIT_AUTOLOAD_SUS_8821C) ? _FALSE : _TRUE;
	/*
	* In 8821C, bautoload_fail_flag is used to present eFuse map is valid
	* or not, no matter the map comes from hardware or files.
	*/

	/* 2. Read eFuse */
	EFUSE_ShadowMapUpdate(adapter, EFUSE_WIFI, 0);

	/* 3. Read Efuse file if necessary */
#ifdef CONFIG_EFUSE_CONFIG_FILE
	if (check_phy_efuse_tx_power_info_valid(adapter) == _FALSE)
		if (Hal_readPGDataFromConfigFile(adapter) != _SUCCESS)
			RTW_INFO("%s: <WARN> invalid phy efuse and read from file fail, will use driver default!!\n", __FUNCTION__);
#endif /* CONFIG_EFUSE_CONFIG_FILE */

	/* 4. Parse Efuse data */
	valid = Hal_EfuseParseIDCode(adapter, efuse_map);
	if (_TRUE == valid)
		hal->bautoload_fail_flag = _FALSE;
	else
		hal->bautoload_fail_flag = _TRUE;

	Hal_EfuseParseEEPROMVer(adapter, efuse_map, valid);
	hal_config_macaddr(adapter, hal->bautoload_fail_flag);
	Hal_EfuseParseTxPowerInfo(adapter, efuse_map, valid);
	Hal_EfuseParseBoardType(adapter, efuse_map, valid);
	Hal_EfuseParseBTCoexistInfo(adapter, efuse_map, valid);
	Hal_EfuseParseChnlPlan(adapter, efuse_map, hal->bautoload_fail_flag);
	Hal_EfuseParseXtal(adapter, efuse_map, valid);
	Hal_EfuseParseThermalMeter(adapter, efuse_map, valid);
	Hal_EfuseParseAntennaDiversity(adapter, efuse_map, valid);
	Hal_EfuseParseCustomerID(adapter, efuse_map, valid);
	Hal_DetectWoWMode(adapter);
	Hal_ReadRFEType(adapter, efuse_map, valid);
	Hal_ReadAmplifierType(adapter, efuse_map, valid);
	Hal_EfuseTxBBSwing(adapter, efuse_map, valid);

	/* Data out of Efuse Map */
	Hal_EfuseParsePackageType(adapter, efuse_map, valid);

#ifdef CONFIG_USB_HCI
	Hal_ReadUsbModeSwitch(adapter, efuse_map, valid);
#endif /* CONFIG_USB_HCI */

#ifdef CONFIG_BT_COEXIST
	if (hal->EEPROMBluetoothCoexist == _TRUE) {
		u8 bMacPwrCtrlOn = _FALSE;
		
		rtw_btcoex_AntInfoSetting(adapter);
		rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		if (bMacPwrCtrlOn == _TRUE)
			rtw_btcoex_PowerOnSetting(adapter);
	}
	else
#endif /* CONFIG_BT_COEXIST */
	rtw_btcoex_wifionly_AntInfoSetting(adapter);

#ifdef CONFIG_RTW_MAC_HIDDEN_RPT
	hal_read_mac_hidden_rpt(adapter);
	{
		struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
		
		if (hal_spec->hci_type <= 3 && hal_spec->hci_type >= 1) {
			hal->EEPROMBluetoothCoexist = _FALSE;
			RTW_INFO("EEPROM Disable BT-coex by hal_spec\n");
			rtw_btcoex_wifionly_AntInfoSetting(adapter);
		}
	}
#endif

	return _SUCCESS;
}

void rtl8821c_run_thread(PADAPTER adapter)
{
}

void rtl8821c_cancel_thread(PADAPTER adapter)
{
}

/*
 * Description:
 *	Using 0x100 to check the power status of FW.
 */
static u8 check_ips_status(PADAPTER adapter)
{
	u8 val8;


	RTW_INFO(FUNC_ADPT_FMT ": Read 0x100=0x%02x 0x86=0x%02x\n",
		 FUNC_ADPT_ARG(adapter),
		 rtw_read8(adapter, 0x100), rtw_read8(adapter, 0x86));

	val8 = rtw_read8(adapter, 0x100);
	if (val8 == 0xEA)
		return _TRUE;

	return _FALSE;
}

static void update_ra_mask_8821c(_adapter *adapter, struct sta_info *psta, struct macid_cfg *h2c_macid_cfg)
{
	u8 arg[4] = {0};

	arg[0] = h2c_macid_cfg->mac_id;
	arg[1] = h2c_macid_cfg->rate_id;
	arg[2] = (h2c_macid_cfg->ignore_bw << 4) | h2c_macid_cfg->short_gi;
	arg[3] = psta->init_rate;

	rtl8821c_set_FwMacIdConfig_cmd(adapter, h2c_macid_cfg->ra_mask, arg, h2c_macid_cfg->bandwidth);
}
static void InitBeaconParameters(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u16 val16;
	u8 val8;


	val8 = BIT_DIS_TSF_UDT_8821C;
	val16 = val8 | (val8 << 8); /* port0 and port1 */
#ifdef CONFIG_BT_COEXIST
	if (hal->EEPROMBluetoothCoexist)
		/* Enable port0 beacon function for PSTDMA under BTCOEX */
		val16 |= EN_BCN_FUNCTION;
#endif
	rtw_write16(adapter, REG_BCN_CTRL_8821C, val16);

	rtw_write16(adapter, REG_TBTT_PROHIBIT_8821C, 0x6404); /* ms */
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8821C + 2, 0);

	rtw_write8(adapter, REG_DRVERLYINT_8821C, DRIVER_EARLY_INT_TIME_8821C); /* 5ms */
	rtw_write8(adapter, REG_BCNDMATIM_8821C, BCN_DMA_ATIME_INT_TIME_8821C); /* 2ms */

	/*
	 * Suggested by designer timchen. Change beacon AIFS to the largest number
	 * beacause test chip does not contension before sending beacon.
	 */
	rtw_write16(adapter, REG_BCNTCFG_8821C, 0x660F);

	hal->RegBcnCtrlVal = rtw_read8(adapter, REG_BCN_CTRL_8821C);
	hal->RegTxPause = rtw_read8(adapter, REG_TXPAUSE_8821C);
	hal->RegFwHwTxQCtrl = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8821C + 2);
	hal->RegCR_1 = rtw_read8(adapter, REG_CR_8821C + 1);
}

static void beacon_function_enable(PADAPTER adapter, u8 Enable, u8 Linked)
{
	u8 val8;
	u32 bcn_ctrl_reg;

	/* port0 */
	bcn_ctrl_reg = REG_BCN_CTRL_8821C;
	val8  = BIT_DIS_TSF_UDT_8821C | BIT_EN_BCN_FUNCTION_8821C;
#ifdef CONFIG_CONCURRENT_MODE
	/* port1 */
	if (adapter->hw_port == HW_PORT1) {
		bcn_ctrl_reg = REG_BCN_CTRL_CLINT0_8821C;
		val8 = BIT_CLI0_DIS_TSF_UDT_8821C | BIT_CLI0_EN_BCN_FUNCTION_8821C;
	}
#endif

	rtw_write8(adapter, bcn_ctrl_reg, val8);
	rtw_write8(adapter, REG_RD_CTRL_8821C + 1, 0x6F);
}

static void set_beacon_related_registers(PADAPTER adapter)
{
	u8 val8;
	u32 value32;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u32 bcn_ctrl_reg, bcn_interval_reg;


	/* reset TSF, enable update TSF, correcting TSF On Beacon */
#if 0
	/* * REG_BCN_INTERVAL */
	/* * REG_BCNDMATIM */
	/* * REG_ATIMWND */
	/* * REG_TBTT_PROHIBIT */
	/* * REG_DRVERLYINT */
	/* * REG_BCN_MAX_ERR */
	/* * REG_BCNTCFG  (0x510) */
	/* * REG_DUAL_TSF_RST */
	/* * REG_BCN_CTRL  (0x550) */
#endif

	bcn_ctrl_reg = REG_BCN_CTRL_8821C;
#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_CLINT0_8821C;
#endif

	/*
	 * ATIM window
	 */
	rtw_write16(adapter, REG_ATIMWND_8821C, 2);

	/*
	 * Beacon interval (in unit of TU).
	 */
#ifdef CONFIG_CONCURRENT_MODE
	/* Port 1 bcn interval */
	if (adapter->hw_port == HW_PORT1) {
		u16 val16 = rtw_read16(adapter, (REG_MBSSID_BCN_SPACE_8821C + 2));

		val16 |= (pmlmeinfo->bcn_interval & BIT_MASK_BCN_SPACE_CLINT0_8821C);
		rtw_write16(adapter, REG_MBSSID_BCN_SPACE_8821C + 2, val16);
	} else
#endif
		/* Port 0 bcn interval */
		rtw_write16(adapter, REG_MBSSID_BCN_SPACE_8821C, pmlmeinfo->bcn_interval);

	InitBeaconParameters(adapter);

	rtw_write8(adapter, REG_SLOT_8821C, 0x09);

	/* Reset TSF Timer to zero */
	val8 = BIT_TSFTR_RST_8821C;
#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1)
		val8 = BIT_TSFTR_CLI0_RST_8821C;
#endif
	rtw_write8(adapter, REG_DUAL_TSF_RST_8821C, val8);
	val8 = BIT_TSFTR_RST_8821C;
	rtw_write8(adapter, REG_DUAL_TSF_RST_8821C, val8);

	rtw_write8(adapter, REG_RXTSF_OFFSET_CCK_8821C, 0x50);
	rtw_write8(adapter, REG_RXTSF_OFFSET_OFDM_8821C, 0x50);

	beacon_function_enable(adapter, _TRUE, _TRUE);

	ResumeTxBeacon(adapter);
}

static void xmit_status_check(PADAPTER p)
{
}

static void linked_status_check(PADAPTER p)
{
}

static const struct hw_port_reg port_cfg[] = {
	/*port 0*/
	{
	.net_type = (REG_CR_8821C + 2),
	.net_type_shift = 0,
	.macaddr = REG_MACID,
	.bssid = REG_BSSID,
	.bcn_ctl = REG_BCN_CTRL,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xffff,
	.ps_aid = REG_BCN_PSR_RPT_8821C,
	},
	/*port 1*/
	{
	.net_type = (REG_CR_8821C + 2),
	.net_type_shift = 2,
	.macaddr = REG_MACID1,
	.bssid = REG_BSSID1,
	.bcn_ctl = REG_BCN_CTRL_CLINT0,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI0_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE,
	.bcn_space_shift = 16,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT1_8821C,
	},
	/*port 2*/
	{
	.net_type =  REG_CR_EXT_8821C,
	.net_type_shift = 0,
	.macaddr = REG_MACID2,
	.bssid = REG_BSSID2,
	.bcn_ctl = REG_BCN_CTRL_CLINT1,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI1_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE2,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT2_8821C,
	},
	/*port 3*/
	{
	.net_type =  REG_CR_EXT_8821C,
	.net_type_shift = 2,
	.macaddr = REG_MACID3,
	.bssid = REG_BSSID3,
	.bcn_ctl = REG_BCN_CTRL_CLINT2,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI2_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE2,
	.bcn_space_shift = 16,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT3_8821C,
	},
	/*port 4*/
	{
	.net_type =  REG_CR_EXT_8821C,
	.net_type_shift = 4,
	.macaddr = REG_MACID4,
	.bssid = REG_BSSID4,
	.bcn_ctl = REG_BCN_CTRL_CLINT3,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI3_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE3,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT4_8821C,
	},
};


static void hw_bcn_ctrl_set(_adapter *adapter, u8 bcn_ctl_val)
{
	u8 hw_port = get_hw_port(adapter);
	u32 bcn_ctl_addr = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	bcn_ctl_addr = port_cfg[hw_port].bcn_ctl;
	rtw_write8(adapter, bcn_ctl_addr, bcn_ctl_val);
}

static void hw_bcn_ctrl_add(_adapter *adapter, u8 bcn_ctl_val)
{
	u8 hw_port = get_hw_port(adapter);
	u32 bcn_ctl_addr = 0;
	u8 val8 = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	bcn_ctl_addr = port_cfg[hw_port].bcn_ctl;
	val8 = rtw_read8(adapter, bcn_ctl_addr) | bcn_ctl_val;
	rtw_write8(adapter, bcn_ctl_addr, val8);
}

static void hw_bcn_ctrl_clr(_adapter *adapter, u8 bcn_ctl_val)
{
	u8 hw_port = get_hw_port(adapter);
	u32 bcn_ctl_addr = 0;
	u8 val8 = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	bcn_ctl_addr = port_cfg[hw_port].bcn_ctl;
	val8 = rtw_read8(adapter, bcn_ctl_addr);
	val8 &= ~bcn_ctl_val;
	rtw_write8(adapter, bcn_ctl_addr, val8);
}


static void hw_bcn_func(_adapter *adapter, u8 enable)
{
	if (enable)
		hw_bcn_ctrl_add(adapter, BIT_EN_BCN_FUNCTION);
	else
		hw_bcn_ctrl_clr(adapter, BIT_EN_BCN_FUNCTION);
}
static void hw_bcn_ctrl_tsf(_adapter *adapter, u8 tsf_udt)
{
	if (tsf_udt)
		hw_bcn_ctrl_clr(adapter, BIT_DIS_TSF_UDT);
	else
		hw_bcn_ctrl_add(adapter, BIT_DIS_TSF_UDT);
}

void hw_port0_tsf_sync_sel(_adapter *adapter, u8 hw_port, u8 benable, u16 tr_offset)
{
	u8 val8;
	u8 client_id = 0;

	if (adapter->tsf.sync_port != MAX_HW_PORT)
		return;

	if (benable && hw_port == HW_PORT0) {
		RTW_ERR(FUNC_ADPT_FMT ": hw_port is port0 under enable\n", FUNC_ADPT_ARG(adapter));
		rtw_warn_on(1);
		return;
	}

	/*Disable Port0's beacon function*/
	hw_bcn_func(adapter, _FALSE);

	/*Reg 0x518[15:0]: TSFTR_SYN_OFFSET*/
	if (tr_offset)
		rtw_write16(adapter, REG_TSFTR_SYN_OFFSET, tr_offset);

	/*reg 0x577[6]=1*/	/*auto sync by tbtt*/
	rtw_write8(adapter, REG_MISC_CTRL, rtw_read8(adapter, REG_MISC_CTRL) | BIT(6));

	if (HW_PORT1 == hw_port)
		client_id = 0;
	else if (HW_PORT2 == hw_port)
		client_id = 1;
	else if (HW_PORT3 == hw_port)
		client_id = 2;
	else if (HW_PORT4 == hw_port)
		client_id = 3;

	/*0x5B4 [6:4] :SYNC_CLI_SEL - The selector for the CLINT port of sync tsft source for port 0*/
	/*	Bit[5:4] : 0 for clint0, 1 for clint1, 2 for clint2, 3 for clint3.
		Bit6 : 1= enable sync to port 0. 0=disable sync to port 0.*/
	val8 = rtw_read8(adapter, REG_TIMER0_SRC_SEL);
	if (benable) {
		val8 &= 0x8F;
		val8 |= (BIT(6) | (client_id << 4));
	} else {
		val8 &= ~BIT(6);
	}
	rtw_write8(adapter, REG_TIMER0_SRC_SEL, val8);

	/*Enable Port0's beacon function*/
	hw_bcn_func(adapter, _TRUE);

	adapter->tsf.sync_port = hw_port;
	adapter->tsf.offset = tr_offset;
}

void hw_port0_tsf_sync(_adapter *adapter)
{
	rtw_write8(adapter, REG_SCHEDULER_RST, rtw_read8(adapter, REG_SCHEDULER_RST) | BIT_SYNC_CLI);
}
void get_tsf_by_port(_adapter *adapter, u8 *tsftr, u8 hw_port)
{
	u8 i;
	u8 val8 = 0;
	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR("%s hw port(%d) invalid\n", __func__, hw_port);
		return;
	}
	/*0x554[30:28] -BIT_BCN_TIMER_SEL_FWRD*/
	val8 = rtw_read8(adapter, REG_MBSSID_BCN_SPACE + 3);
	val8 &= 0x8F;
	val8 |= hw_port << 4;
	rtw_write8(adapter, REG_MBSSID_BCN_SPACE + 3, val8);

	for (i = 0; i < 8; i++)
		tsftr[i] = rtw_read8(adapter, REG_TSFTR+i);
}

void hw_get_tsftr(_adapter *adapter, u8 *tsftr)
{
	if (!tsftr) {
		RTW_ERR("%s tsftr is NULL\n", __func__);
		return;
	}

	get_tsf_by_port(adapter, tsftr, adapter->hw_port);
}

void hw_tsf_reset(_adapter *adapter)
{
	u8 hw_port = get_hw_port(adapter);
	u32 tsf_rst_addr = 0;
	u8 val8 = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	tsf_rst_addr = port_cfg[hw_port].tsf_rst;
	val8 = rtw_read8(adapter, tsf_rst_addr) | (port_cfg[hw_port].tsf_rst_bit);
	rtw_write8(adapter, tsf_rst_addr, val8);
}

#ifdef CONFIG_TSF_RESET_OFFLOAD
static int reset_tsf(PADAPTER adapter, u8 reset_port)
{
	u8 reset_cnt_before = 0, reset_cnt_after = 0, loop_cnt = 0;
	u32 reg_reset_tsf_cnt = (HW_PORT0 == reset_port) ?
				REG_FW_RESET_TSF_CNT_0 : REG_FW_RESET_TSF_CNT_1;


	/* site survey will cause reset_tsf fail */
	rtw_mi_buddy_scan_abort(adapter, _FALSE);
	reset_cnt_after = reset_cnt_before = rtw_read8(adapter, reg_reset_tsf_cnt);
	rtl8821c_reset_tsf(adapter, reset_port);

	while ((reset_cnt_after == reset_cnt_before) && (loop_cnt < 10)) {
		rtw_msleep_os(100);
		loop_cnt++;
		reset_cnt_after = rtw_read8(adapter, reg_reset_tsf_cnt);
	}

	return (loop_cnt >= 10) ? _FAIL : _TRUE;
}
#endif /* CONFIG_TSF_RESET_OFFLOAD */

static void hw_var_set_macaddr(PADAPTER adapter, u8 *val)
{
	if (!val) {
		RTW_INFO(ADPT_FMT "set mac address(NULL) failed\n", ADPT_ARG(adapter));
		rtw_warn_on(1);
	}
	rtw_halmac_set_mac_address(adapter_to_dvobj(adapter), adapter->hw_port, val);
}

static void hw_var_set_bssid(PADAPTER adapter, u8 *val)
{
	if (!val) {
		RTW_INFO(ADPT_FMT "set bssid (NULL) failed\n", ADPT_ARG(adapter));
		rtw_warn_on(1);
	}

	rtw_halmac_set_bssid(adapter_to_dvobj(adapter), adapter->hw_port, val);
}


static void hw_var_set_monitor(PADAPTER Adapter, u8 *val)
{
	u32	rcr_bits;
	u8 mode;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);

	mode = *((u8 *)val);

	if (mode != _HW_STATE_MONITOR_) {
		RTW_ERR(FUNC_ADPT_FMT" mode(0x%02x) invalid\n", FUNC_ADPT_ARG(Adapter), mode);
		return;
	}

	/* Receive all type */
	rcr_bits = RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_APWRMGT | RCR_ADF | RCR_ACF | RCR_AMF | RCR_APP_PHYST_RXFF;

	#ifdef CONFIG_RX_PACKET_APPEND_FCS
	/* Append FCS */
	rcr_bits |= RCR_APPFCS;
	#endif

	#ifdef CONFIG_RX_PACKET_APPEND_CRC
	/*CRC and ICV packet will drop in rx func*/
	rcr_bits |= (RCR_ACRC32 | RCR_AICV);
	#endif

	rtl8821c_rcr_config(Adapter, rcr_bits);
	/* Receive all data frames */
	rtw_write16(Adapter, REG_RXFLTMAP_8821C, 0xFFFF);

	#if 0
	/* tx pause */
	rtw_write8(padapter, REG_TXPAUSE, 0xFF);
	#endif
}

static void hw_var_set_opmode(PADAPTER Adapter, u8 *val)
{
	u8	mode = *((u8 *)val);
	static u8 isMonitor = _FALSE;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if (isMonitor == _TRUE) {
		/* reset RCR */
		rtl8821c_rcr_config(Adapter, pHalData->ReceiveConfig);
		isMonitor = _FALSE;
	}

	if (mode == _HW_STATE_MONITOR_) {
		isMonitor = _TRUE;
		/* set net_type */
		Set_MSR(Adapter, _HW_STATE_NOLINK_);

		hw_var_set_monitor(Adapter, val);
		return;
	}

	Adapter->tsf.sync_port =  MAX_HW_PORT;
	Adapter->tsf.offset = 0;

	rtw_hal_set_hwreg(Adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(Adapter)); /* set mac addr to mac register */
	RTW_INFO(ADPT_FMT ": hw_port(%d) set mode=%d\n", ADPT_ARG(Adapter), get_hw_port(Adapter), mode);

#ifdef CONFIG_MI_WITH_MBSSID_CAM /*For Port0 - MBSS CAM*/
	if (Adapter->hw_port != HW_PORT0) {
		RTW_ERR(ADPT_FMT ": Configure MBSSID cam on HW_PORT%d\n", ADPT_ARG(Adapter), Adapter->hw_port);
		rtw_warn_on(1);
	} else
		hw_var_set_opmode_mbid(Adapter, mode);
#else

	/* set net_type */
	Set_MSR(Adapter, mode);

	if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
		#ifdef CONFIG_CONCURRENT_MODE
		if (!rtw_mi_check_status(Adapter, MI_AP_MODE))
		#endif
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* restore early int time to 5ms */
#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, 0, SDIO_HIMR_BCNERLY_INT_MSK);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, 0, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK));
#endif
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */
#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

			StopTxBeacon(Adapter);
			hw_bcn_ctrl_set(Adapter, BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C);

	} else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/) {
		ResumeTxBeacon(Adapter);
		hw_bcn_ctrl_set(Adapter, BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C);

	} else if (mode == _HW_STATE_AP_) {

		if (Adapter->hw_port == HW_PORT0) {
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, SDIO_HIMR_BCNERLY_INT_MSK, 0);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK), 0);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */
#endif /* CONFIG_INTERRUPT_BASED_TXBCN */
			ResumeTxBeacon(Adapter);
			rtl8821c_rcr_clear(Adapter, RCR_CBSSID_DATA);/* Clear CBSSID_DATA */

			/* for 11n Logo 4.2.31/4.2.32, disable BSSID BCN check for AP mode */
			if (Adapter->registrypriv.wifi_spec)
				rtl8821c_rcr_clear(Adapter, RCR_CBSSID_BCN);

			/* enable to rx data frame */
			rtw_write16(Adapter, REG_RXFLTMAP_8821C, 0xFFFF);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM_8821C, 0x02); /* 2ms	*/

			rtw_write8(Adapter, REG_ATIMWND_8821C, 0x0c); /* ATIM:12ms */
			rtw_write16(Adapter, REG_BCNTCFG_8821C, 0x00);

			/*TBTT hold time :4ms, setup time:128 us*/
			rtw_write32(Adapter, REG_TBTT_PROHIBIT_8821C,
				(rtw_read32(Adapter, REG_TBTT_PROHIBIT_8821C) & (~0xFFFFF)) | 0x08004);

			/*rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET_8821C, 0x7fff);*//* +32767 (~32ms) */
			hw_tsf_reset(Adapter);
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
#if defined(CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR)
			hw_bcn_ctrl_set(Adapter, BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C | BIT_P0_EN_TXBCN_RPT_8821C);
#else
			hw_bcn_ctrl_set(Adapter, BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C);
#endif
			/*TODO  hw_port0_tsf_sync_sel must modify for multi-interface && multi-port*/
			if (rtw_mi_buddy_check_fwstate(Adapter, (WIFI_STATION_STATE | WIFI_ASOC_STATE)))
				hw_port0_tsf_sync_sel(Adapter, HW_PORT1, _TRUE, 0x32);/* About offset = 50ms*/

		} else {
			RTW_ERR(ADPT_FMT ": set AP mode on HW_PORT%d\n", ADPT_ARG(Adapter), Adapter->hw_port);
			rtw_warn_on(1);
		}
	}
#endif
}

#ifdef CONFIG_AP_PORT_SWAP
void rtw_hal_port_reconfig(_adapter *adapter, u8 port)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 bssid_offset = 0;
	u8 bssid[6] = {0};
	u8 vnet_type = 0;
	u8 vbcn_ctrl = 0;
	u8 i;

	if (port > (hal_spec->port_num - 1)) {
		RTW_INFO("[WARN] "ADPT_FMT"- hw_port : %d,will switch to invalid port-%d\n",
			 ADPT_ARG(adapter), adapter->hw_port, port);
		rtw_warn_on(1);
	}

	RTW_PRINT(ADPT_FMT" - hw_port : %d,will switch to port-%d\n",
		  ADPT_ARG(adapter), adapter->hw_port, port);

	/*backup*/
	vnet_type = (rtw_read8(adapter, port_cfg[adapter->hw_port].net_type) >> port_cfg[adapter->hw_port].net_type_shift) & 0x03;
	vbcn_ctrl = rtw_read8(adapter, port_cfg[adapter->hw_port].bcn_ctl);

	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)) {
		RTW_INFO("port0-iface is STA mode and linked\n");
		bssid_offset = port_cfg[adapter->hw_port].bssid;
		for (i = 0; i < 6; i++)
			bssid[i] = rtw_read8(adapter, bssid_offset + i);
	}
	/*reconfigure*/
	adapter->hw_port = port;
	hw_var_set_macaddr(adapter, adapter_mac_addr(adapter));
	Set_MSR(adapter, vnet_type);

	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)) {
		hw_var_set_bssid(adapter, bssid);
		hw_tsf_reset(adapter);
	}

	rtw_write8(adapter, port_cfg[adapter->hw_port].bcn_ctl, vbcn_ctrl);
}
static void rtl8821c_ap_port_switch(_adapter *adapter, u8 mode)
{
	u8 hw_port = get_hw_port(adapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 ap_nums = 0;
	_adapter *if_port0 = NULL;
	int i;

	RTW_INFO(ADPT_FMT ": hw_port(%d) will set mode to %d\n", ADPT_ARG(adapter), hw_port, mode);
#if 0
	#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(&adapter->wdinfo, P2P_STATE_NONE)) {
		RTW_INFO("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__,
			rtw_p2p_role(&adapter->wdinfo), rtw_p2p_state(&adapter->wdinfo), rtw_p2p_pre_state(&adapter->wdinfo));
	}
	#endif
#endif

	if (mode != _HW_STATE_AP_)
		return;

	if ((mode == _HW_STATE_AP_) && (hw_port == HW_PORT0))
		return;

	/*check and prepare switch port to port0 for AP mode's BCN function*/
	ap_nums = rtw_mi_get_ap_num(adapter);
	if (ap_nums > 0) {
		RTW_ERR("SortAP mode numbers:%d, must move setting to MBSSID CAM, not support yet\n", ap_nums);
		rtw_warn_on(1);
		return;
	}

	/*Get iface of port-0*/
	for (i = 0; i < dvobj->iface_nums; i++) {
		if (get_hw_port(dvobj->padapters[i]) == HW_PORT0) {
			if_port0 = dvobj->padapters[i];
			break;
		}
	}

	if (if_port0 == NULL) {
		RTW_ERR("%s if_port0 == NULL\n", __func__);
		rtw_warn_on(1);
		return;
	}
	rtw_hal_port_reconfig(if_port0, hw_port);

	adapter->hw_port = HW_PORT0;
	RTW_INFO(ADPT_FMT ": Cfg SoftAP mode to hw_port(%d) done\n", ADPT_ARG(adapter), adapter->hw_port);

}
#endif

static void hw_var_set_basic_rate(PADAPTER adapter, u8 *ratetbl)
{
#define RATE_1M		BIT(0)
#define RATE_2M		BIT(1)
#define RATE_5_5M	BIT(2)
#define RATE_11M	BIT(3)
#define RATE_6M		BIT(4)
#define RATE_9M		BIT(5)
#define RATE_12M	BIT(6)
#define RATE_18M	BIT(7)
#define RATE_24M	BIT(8)
#define RATE_36M	BIT(9)
#define RATE_48M	BIT(10)
#define RATE_54M	BIT(11)
#define RATE_MCS0	BIT(12)
#define RATE_MCS1	BIT(13)
#define RATE_MCS2	BIT(14)
#define RATE_MCS3	BIT(15)
#define RATE_MCS4	BIT(16)
#define RATE_MCS5	BIT(17)
#define RATE_MCS6	BIT(18)
#define RATE_MCS7	BIT(19)

#define RATES_CCK	(RATE_11M | RATE_5_5M | RATE_2M | RATE_1M)
#define RATES_OFDM	(RATE_54M | RATE_48M | RATE_36M | RATE_24M | RATE_18M | RATE_12M | RATE_9M | RATE_6M)

	struct mlme_ext_info *mlmext_info = &adapter->mlmeextpriv.mlmext_info;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u16 input_b = 0, masked = 0, ioted = 0, BrateCfg = 0;
	u16 rrsr_2g_force_mask = RATES_CCK;
	u16 rrsr_2g_allow_mask = RATE_24M | RATE_12M | RATE_6M | RATES_CCK;
	u16 rrsr_5g_force_mask = RATE_6M;
	u16 rrsr_5g_allow_mask = RATES_OFDM;
	u32 val32;

	HalSetBrateCfg(adapter, ratetbl, &BrateCfg);
	input_b = BrateCfg;

	/* apply force and allow mask */
	if (hal->current_band_type == BAND_ON_2_4G) {
		BrateCfg |= rrsr_2g_force_mask;
		BrateCfg &= rrsr_2g_allow_mask;
	} else {
		BrateCfg |= rrsr_5g_force_mask;
		BrateCfg &= rrsr_5g_allow_mask;
	}

	masked = BrateCfg;

	/* IOT consideration */
	if (mlmext_info->assoc_AP_vendor == HT_IOT_PEER_CISCO) {
		/* if peer is cisco and didn't use ofdm rate, we enable 6M ack */
		if ((BrateCfg & (RATE_24M | RATE_12M | RATE_6M)) == 0)
			BrateCfg |= RATE_6M;
	}

	ioted = BrateCfg;

	hal->BasicRateSet = BrateCfg;

	RTW_INFO("[HW_VAR_BASIC_RATE] %#x->%#x->%#x\n", input_b, masked, ioted);

	/* Set RRSR rate table. */
	val32 = rtw_read32(adapter, REG_RRSR_8821C);
	val32 &= ~(BIT_MASK_RRSC_BITMAP << BIT_SHIFT_RRSC_BITMAP);
	val32 |= BIT_RRSC_BITMAP(BrateCfg);
	val32 = rtw_write32(adapter, REG_RRSR_8821C, val32);
}

static void hw_var_set_bcn_func(PADAPTER adapter, u8 enable)
{
	u8 val8;

	if (enable) {
		/* enable TX BCN report
		 *  Reg REG_FWHW_TXQ_CTRL_8821C[2] = 1
		 *  Reg REG_BCN_CTRL_8821C[3][5] = 1
		 */
		val8 = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8821C);
		val8 |= BIT_EN_BCN_TRXRPT_V1_8821C;
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8821C, val8);

		if (adapter->hw_port == HW_PORT0)
			hw_bcn_ctrl_add(adapter, BIT_EN_BCN_FUNCTION_8821C | BIT_P0_EN_TXBCN_RPT_8821C);
		else
			hw_bcn_ctrl_add(adapter, BIT_EN_BCN_FUNCTION_8821C);
	} else {
		if (adapter->hw_port == HW_PORT0) {
			val8 = BIT_EN_BCN_FUNCTION_8821C | BIT_P0_EN_TXBCN_RPT_8821C;

			#ifdef CONFIG_BT_COEXIST
			/* Always enable port0 beacon function for PSTDMA */
			if (GET_HAL_DATA(adapter)->EEPROMBluetoothCoexist)
				val8 = BIT_P0_EN_TXBCN_RPT_8821C;
			#endif
			hw_bcn_ctrl_clr(adapter, val8);
		} else
			hw_bcn_ctrl_clr(adapter, BIT_EN_BCN_FUNCTION_8821C);
	}
}

static void hw_var_set_correct_tsf(PADAPTER Adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	/*do nothing*/
#else
#if 1
	/*hw_tsf_reset(Adapter);*/

	#ifdef CONFIG_CONCURRENT_MODE
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	/* Update buddy port's TSF if it is SoftAP for beacon TX issue!*/
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE
		&& rtw_mi_check_status(Adapter, MI_AP_MODE)) {

		struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
		int i;
		_adapter *iface;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;
			if (iface == Adapter)
				continue;

			if (	check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE 
				&& check_fwstate(&iface->mlmepriv, WIFI_ASOC_STATE) == _TRUE
			) {
				hw_port0_tsf_sync_sel(Adapter, Adapter->hw_port, _TRUE, 0x32);/* About offset = 50ms*/
				break;
			}
		}
	} else if (((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)
									&& (Adapter->hw_port == HW_PORT0))
	#endif /*CONFIG_CONCURRENT_MODE*/
		/* disable func of port0 TSF sync from another port*/
		hw_port0_tsf_sync_sel(Adapter, Adapter->hw_port, _FALSE, 0);
#else
	u64 tsf;
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/*tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; */
	tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval * 1024)) - 1024; /*us*/

	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
		/*pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
		rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)|BIT(6)));*/
		StopTxBeacon(Adapter);
	}

	rtw_hal_correct_tsf(Adapter, Adapter->hw_port, tsf);

#ifdef CONFIG_CONCURRENT_MODE
	/* Update buddy port's TSF if it is SoftAP for beacon TX issue!*/
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE
	    && rtw_mi_check_status(Adapter, MI_AP_MODE)) {

		struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
		int i;
		_adapter *iface;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;
			if (iface == Adapter)
				continue;

			if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE
			    && check_fwstate(&iface->mlmepriv, WIFI_ASOC_STATE) == _TRUE
			   ) {
				rtw_hal_correct_tsf(iface, iface->hw_port, tsf);
#ifdef CONFIG_TSF_RESET_OFFLOAD
				if (reset_tsf(iface, iface->hw_port) == _FALSE)
					RTW_INFO("%s-[ERROR] "ADPT_FMT" Reset port%d TSF fail\n", __func__, ADPT_ARG(iface), iface->hw_port);
#endif	/* CONFIG_TSF_RESET_OFFLOAD*/
			}
		}
	}
#endif /*CONFIG_CONCURRENT_MODE*/

	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)) {
		/*pHalData->RegTxPause	&= (~STOP_BCNQ);
		rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)&(~BIT(6))));*/
		ResumeTxBeacon(Adapter);
	}
#endif
#endif/*CONFIG_MI_WITH_MBSSID_CAM*/
}

static void hw_var_set_check_bssid(PADAPTER adapter, u8 enable)
{
	u32 rcr;

	rcr = BIT_CBSSID_DATA_8821C | BIT_CBSSID_BCN_8821C;
	if (enable)
		rtl8821c_rcr_add(adapter, rcr);
	else
		rtl8821c_rcr_clear(adapter, rcr);

	RTW_INFO("%s: [HW_VAR_CHECK_BSSID] 0x%x=0x%x\n", __FUNCTION__,
		 REG_RCR_8821C, rtw_read32(adapter, REG_RCR_8821C));
}

static void hw_var_set_mlme_disconnect(PADAPTER adapter)
{
	u8 val8;

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
#endif
		/* reject all data frames under not link state */
		rtw_write16(adapter, REG_RXFLTMAP_8821C, 0);

	/* reset TSF*/
	hw_tsf_reset(adapter);

	/* disable update TSF*/
	hw_bcn_ctrl_tsf(adapter, _FALSE);

	/* disable Port's beacon function */
	hw_bcn_func(adapter, _FALSE);
}

static void hw_var_set_mlme_sitesurvey(PADAPTER adapter, u8 enable)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u32 rcr_bit;
	u16 value_rxfltmap2;
	u8 val8;
	PHAL_DATA_TYPE hal;
	struct mlme_priv *pmlmepriv;
	int i;
	_adapter *iface;


#ifdef DBG_IFACE_STATUS
	DBG_IFACE_STATUS_DUMP(adapter);
#endif

	hal = GET_HAL_DATA(adapter);
	pmlmepriv = &adapter->mlmepriv;

#ifdef CONFIG_FIND_BEST_CHANNEL
	rcr_bit = BIT_CBSSID_BCN_8821C | BIT_CBSSID_DATA_8821C;

	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;
#else /* CONFIG_FIND_BEST_CHANNEL */

	rcr_bit = BIT_CBSSID_BCN_8821C;

	/* config RCR to receive different BSSID & not to receive data frame */
	value_rxfltmap2 = 0;
#endif /* CONFIG_FIND_BEST_CHANNEL */

	if (rtw_mi_check_fwstate(adapter, WIFI_AP_STATE))
		rcr_bit = BIT_CBSSID_BCN_8821C;

#ifdef CONFIG_TDLS
	/* TDLS will clear RCR_CBSSID_DATA bit for connection. */
	else if (adapter->tdlsinfo.link_established == _TRUE)
		rcr_bit = BIT_CBSSID_BCN_8821C;
#endif /* CONFIG_TDLS */

	if (enable) {
		/*
		 * 1. configure REG_RXFLTMAP2
		 * 2. disable TSF update &  buddy TSF update to avoid updating wrong TSF due to clear RCR_CBSSID_BCN
		 * 3. config RCR to receive different BSSID BCN or probe rsp
		 */

		rtw_write16(adapter, REG_RXFLTMAP_8821C, value_rxfltmap2);


#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*do nothing~~*/
#else

		/* disable update TSF */
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;

			if (rtw_linked_check(iface) &&
			    check_fwstate(&(iface->mlmepriv), WIFI_AP_STATE) != _TRUE) {
				hw_bcn_ctrl_tsf(iface, _FALSE);
				iface->mlmeextpriv.en_hw_update_tsf = _FALSE;
			}

		}
#endif/*CONFIG_MI_WITH_MBSSID_CAM*/

		rtl8821c_rcr_clear(adapter, rcr_bit);

		/* Save orignal RRSR setting. */
		hal->RegRRSR = rtw_read16(adapter, REG_RRSR_8821C);

		if (rtw_mi_check_status(adapter, MI_AP_MODE))
			StopTxBeacon(adapter);
	} else {
		/* sitesurvey done
		 * 1. enable rx data frame
		 * 2. config RCR not to receive different BSSID BCN or probe rsp
		 * 3. can enable TSF update &  buddy TSF right now due to HW support(IC before 8821C not support ex:8812A/8814A/8192E...)
		 */
		if (rtw_mi_check_fwstate(adapter, _FW_LINKED | WIFI_AP_STATE))/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP_8821C, 0xFFFF);

		/* for 11n Logo 4.2.31/4.2.32, disable BSSID BCN check for AP mode */
		if (adapter->registrypriv.wifi_spec && MLME_IS_AP(adapter))
			rcr_bit &= ~(BIT_CBSSID_BCN_8821C);

		rtl8821c_rcr_add(adapter, rcr_bit);
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*if ((rtw_mi_get_assoced_sta_num(Adapter) == 1) && (!rtw_mi_check_status(Adapter, MI_AP_MODE)))
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~DIS_TSF_UDT));*/
#else

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;
			if (rtw_linked_check(iface) &&
			    check_fwstate(&(iface->mlmepriv), WIFI_AP_STATE) != _TRUE) {
				/* enable HW TSF update when receive beacon*/
				hw_bcn_ctrl_tsf(iface, _TRUE);
				iface->mlmeextpriv.en_hw_update_tsf = _FALSE;
			}
		}
#endif

		/* Restore orignal RRSR setting. */
		rtw_write16(adapter, REG_RRSR_8821C, hal->RegRRSR);

		if (rtw_mi_check_status(adapter, MI_AP_MODE)) {
			ResumeTxBeacon(adapter);
			rtw_mi_tx_beacon_hdl(adapter);
		}
	}
}
static void hw_var_set_mlme_join(PADAPTER adapter, u8 type)
{
	u8 val8;
	u16 val16;
	u32 val32;
	u8 RetryLimit;
	PHAL_DATA_TYPE hal;
	struct mlme_priv *pmlmepriv;

	RetryLimit = 0x30;
	hal = GET_HAL_DATA(adapter);
	pmlmepriv = &adapter->mlmepriv;


#ifdef CONFIG_CONCURRENT_MODE
	if (type == 0) {
		/* prepare to join */
		if (rtw_mi_check_status(adapter, MI_AP_MODE))
			StopTxBeacon(adapter);

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP_8821C, 0xFFFF);
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && (rtw_mi_get_assoced_sta_num(Adapter) == 1))
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
		else if ((rtw_mi_get_ap_num(Adapter) == 1) && (rtw_mi_get_assoced_sta_num(Adapter) == 1))
			rtw_write32(adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
		else*/

		rtl8821c_rcr_clear(adapter, BIT_CBSSID_DATA_8821C | BIT_CBSSID_BCN_8821C);
#else

		if (rtw_mi_check_status(adapter, MI_AP_MODE))
			val32 = BIT_CBSSID_BCN_8821C;
		else
			val32 = BIT_CBSSID_DATA_8821C | BIT_CBSSID_BCN_8821C;
		rtl8821c_rcr_add(adapter, val32);
#endif
		hw_bcn_func(adapter, _TRUE);
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? 7 : 48;
		else /* Ad-hoc Mode */
			RetryLimit = 0x7;

	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
			rtw_write16(adapter, REG_RXFLTMAP_8821C, 0x00);

		if (rtw_mi_check_status(adapter, MI_AP_MODE))  {
			ResumeTxBeacon(adapter);

			/* reset TSF 1/2 after resume_tx_beacon */
			val8 = BIT_TSFTR_RST_8821C | BIT_TSFTR_CLI0_RST_8821C;
			rtw_write8(adapter, REG_DUAL_TSF_RST_8821C, val8);
		}
	} else if (type == 2) {
		/* sta add event callback */
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		/*if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && (rtw_mi_get_assoced_sta_num(padapter) == 1))
			rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~DIS_TSF_UDT));*/
#else
		/* enable update TSF */
		hw_bcn_ctrl_tsf(adapter, _TRUE);
#endif
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			rtw_write8(adapter, 0x542, 0x02);
			RetryLimit = 0x7;
		}

		if (rtw_mi_check_status(adapter, MI_AP_MODE)) {
			ResumeTxBeacon(adapter);

			/* reset TSF 1/2 after resume_tx_beacon */
			rtw_write8(adapter, REG_DUAL_TSF_RST_8821C, BIT_TSFTR_RST_8821C | BIT_TSFTR_CLI0_RST_8821C);
		}
	}

	val16 = BIT_LRL_8821C(RetryLimit) | BIT_SRL_8821C(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT_8821C, val16);
#else /* !CONFIG_CONCURRENT_MODE */
	if (type == 0) {
		/* prepare to join */

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP_8821C, 0xFFFF);

		hw_var_set_check_bssid(adapter, !adapter->in_cta_test);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? 7 : 48;
		else /* Ad-hoc Mode */
			RetryLimit = 0x7;
		hw_bcn_func(adapter, _TRUE);
	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		rtw_write16(adapter, REG_RXFLTMAP_8821C, 0x00);
	} else if (type == 2) {
		/* sta add event callback */

		/* enable update TSF */
		hw_bcn_ctrl_tsf(adapter, _TRUE);

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
			RetryLimit = 0x7;
	}

	val16 = BIT_LRL_8821C(RetryLimit) | BIT_SRL_8821C(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT_8821C, val16);
#endif /* !CONFIG_CONCURRENT_MODE */
}

static void hw_var_set_acm_ctrl(PADAPTER adapter, u8 ctrl)
{
	u8 hwctrl = 0;

	if (ctrl) {
		hwctrl |= BIT_ACMHWEN_8821C;

		if (ctrl & BIT(1)) /* BE */
			hwctrl |= BIT_BEQ_ACM_EN_8821C;
		else
			hwctrl &= (~BIT_BEQ_ACM_EN_8821C);

		if (ctrl & BIT(2)) /* VI */
			hwctrl |= BIT_VIQ_ACM_EN_8821C;
		else
			hwctrl &= (~BIT_VIQ_ACM_EN_8821C);

		if (ctrl & BIT(3)) /* VO */
			hwctrl |= BIT_VOQ_ACM_EN_8821C;
		else
			hwctrl &= (~BIT_VOQ_ACM_EN_8821C);
	}

	RTW_INFO("[HW_VAR_ACM_CTRL] Write 0x%02X\n", hwctrl);
	rtw_write8(adapter, REG_ACMHWCTRL_8821C, hwctrl);
}

static void hw_var_set_rcr_am(PADAPTER adapter, u8 enable)
{
	u32 rcr;

	rcr = BIT_AM_8821C;
	if (enable)
		rtl8821c_rcr_add(adapter, rcr);
	else
		rtl8821c_rcr_clear(adapter, rcr);

	RTW_INFO("%s: [HW_VAR_ON_RCR_AM] RCR(0x%x)=0x%x\n",
		__FUNCTION__, REG_RCR_8821C, rtw_read32(adapter, REG_RCR_8821C));
}

static void hw_var_set_bcn_interval(PADAPTER adapter, u16 bcn_interval)
{

	u8 hw_port = get_hw_port(adapter);
	u32 bcn_interval_addr = 0;
	u8 bcn_interval_shift = 0;
	u16 bcn_interval_mask = 0;
	u32 val32 = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	bcn_interval_addr = port_cfg[hw_port].bcn_space;
	bcn_interval_shift = port_cfg[hw_port].bcn_space_shift;
	bcn_interval_mask = port_cfg[hw_port].bcn_space_mask;

	val32 = rtw_read32(adapter, bcn_interval_addr);
	val32 &= (~(bcn_interval_mask << bcn_interval_shift));
	val32 |= (bcn_interval & bcn_interval_mask) << bcn_interval_shift;

	rtw_write32(adapter, bcn_interval_addr, val32);

	RTW_INFO(ADPT_FMT" BEACON_INTERVAL : 0x%x = 0x%x\n", ADPT_ARG(adapter),
		bcn_interval_addr, (rtw_read32(adapter, bcn_interval_addr) >> bcn_interval_shift) & bcn_interval_mask);
}

static void hw_var_set_sec_cfg(PADAPTER adapter, u8 cfg)
{
	u16 reg_scr_ori;
	u16 reg_scr;

	reg_scr = reg_scr_ori = rtw_read16(adapter, REG_SECCFG_8821C);
	reg_scr |= (BIT_CHK_KEYID_8821C | BIT_RXDEC_8821C | BIT_TXENC_8821C);

	if (_rtw_camctl_chk_cap(adapter, SEC_CAP_CHK_BMC))
		reg_scr |= BIT_CHK_BMC_8821C;

	if (_rtw_camctl_chk_flags(adapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH))
		reg_scr |= BIT_NOSKMC_8821C;

	if (reg_scr != reg_scr_ori)
		rtw_write16(adapter, REG_SECCFG_8821C, reg_scr);

	RTW_INFO("%s: [HW_VAR_SEC_CFG] 0x%x=0x%x\n", __FUNCTION__,
		 REG_SECCFG_8821C, rtw_read32(adapter, REG_SECCFG_8821C));
}

static void hw_var_set_sec_dk_cfg(PADAPTER adapter, u8 enable)
{
	struct security_priv *sec = &adapter->securitypriv;
	u8 reg_scr = rtw_read8(adapter, REG_SECCFG_8821C);

	if (enable) {
		/* Enable default key related setting */
		reg_scr |= BIT_TXBCUSEDK_8821C;
		if (sec->dot11AuthAlgrthm != dot11AuthAlgrthm_8021X)
			reg_scr |= BIT_RXUHUSEDK_8821C | BIT_TXUHUSEDK_8821C;
	} else {
		/* Disable default key related setting */
		reg_scr &= ~(BIT_RXBCUSEDK_8821C | BIT_TXBCUSEDK_8821C | BIT_RXUHUSEDK_8821C | BIT_TXUHUSEDK_8821C);
	}

	rtw_write8(adapter, REG_SECCFG_8821C, reg_scr);

	RTW_INFO("%s: [HW_VAR_SEC_DK_CFG] 0x%x=0x%08x\n", __FUNCTION__,
		 REG_SECCFG_8821C, rtw_read32(adapter, REG_SECCFG_8821C));
}

static void hw_var_set_bcn_valid(PADAPTER adapter)
{
	u8 val8 = 0;

	/* only port 0 can TX BCN */
	val8 = rtw_read8(adapter, REG_FIFOPAGE_CTRL_2_8821C + 1);
	val8 = val8 | BIT(7);
	rtw_write8(adapter, REG_FIFOPAGE_CTRL_2_8821C + 1, val8);
}

static void hw_var_set_cam_empty_entry(PADAPTER adapter, u8 ucIndex)
{
	u8 i;
	u32 ulCommand = 0;
	u32 ulContent = 0;
	u32 ulEncAlgo = CAM_AES;

	for (i = 0; i < CAM_CONTENT_COUNT; i++) {
		/* filled id in CAM config 2 byte */
		if (i == 0)
			ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo) << 2);
		else
			ulContent = 0;

		/* polling bit, and No Write enable, and address */
		ulCommand = CAM_CONTENT_COUNT * ucIndex + i;
		ulCommand |= BIT_SECCAM_POLLING_8821C | BIT_SECCAM_WE_8821C;
		/* write content 0 is equall to mark invalid */
		rtw_write32(adapter, REG_CAMWRITE_8821C, ulContent);
		rtw_write32(adapter, REG_CAMCMD_8821C, ulCommand);
	}
}

static void hw_var_set_ack_preamble(PADAPTER adapter, u8 bShortPreamble)
{
	u8 val8 = 0;


	val8 = rtw_read8(adapter, REG_WMAC_TRXPTCL_CTL_8821C + 2);
	val8 |= BIT(4) | BIT(5);

	if (bShortPreamble)
		val8 |= BIT1;
	else
		val8 &= (~BIT1);

	rtw_write8(adapter, REG_WMAC_TRXPTCL_CTL_8821C + 2, val8);
}

void hw_var_set_dl_rsvd_page(PADAPTER adapter, u8 mstatus)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	BOOLEAN bcn_valid = _FALSE;
	u8 DLBcnCount = 0;
	u32 poll = 0;
	u8 val8;


	RTW_INFO(FUNC_ADPT_FMT ":+ hw_port=%d mstatus(%x)\n",
		 FUNC_ADPT_ARG(adapter), get_hw_port(adapter), mstatus);

	if (mstatus == RT_MEDIA_CONNECT) {
		BOOLEAN bRecover = _FALSE;
		u8 v8;

		/* We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 8821C -bit 10:0 */
		rtw_write16(adapter, port_cfg[get_hw_port(adapter)].ps_aid, (0xF800 | pmlmeinfo->aid));

		/* Enable SW TX beacon - Set REG_CR bit 8. DMA beacon by SW */
		v8 = rtw_read8(adapter, REG_CR_8821C + 1);
		v8 |= (BIT_ENSWBCN_8821C >> 8);
		rtw_write8(adapter, REG_CR_8821C + 1, v8);

		/*
		 * Disable Hw protection for a time which revserd for Hw sending beacon.
		 * Fix download reserved page packet fail that access collision with the protection time.
		 */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8821C);
		val8 &= ~BIT_EN_BCN_FUNCTION_8821C;
		val8 |= BIT_DIS_TSF_UDT_8821C;
		rtw_write8(adapter, REG_BCN_CTRL_8821C, val8);

#if 0
		/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		if (hal->RegFwHwTxQCtrl & BIT(6))
			bRecover = _TRUE;

		/* To tell Hw the packet is not a real beacon frame. */
		val8 = hal->RegFwHwTxQCtrl & ~BIT(6);
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8821C + 2, val8);
		hal->RegFwHwTxQCtrl &= ~BIT(6);
#endif

		/* Clear beacon valid check bit. */
		rtw_hal_set_hwreg(adapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(adapter, HW_VAR_DL_BCN_SEL, NULL);

		DLBcnCount = 0;
		poll = 0;
		do {
			/* download rsvd page. */
			rtw_hal_set_fw_rsvd_page(adapter, 0);
			DLBcnCount++;
			do {
				rtw_yield_os();

				/* check rsvd page download OK. */
				rtw_hal_get_hwreg(adapter, HW_VAR_BCN_VALID, (u8 *)(&bcn_valid));
				poll++;
			} while (!bcn_valid && (poll % 10) != 0 && !RTW_CANNOT_RUN(adapter));

		} while (!bcn_valid && DLBcnCount <= 100 && !RTW_CANNOT_RUN(adapter));

		if (RTW_CANNOT_RUN(adapter))
			;
		else if (!bcn_valid)
			RTW_INFO(FUNC_ADPT_FMT ": DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				 FUNC_ADPT_ARG(adapter), DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
			pwrctl->fw_psmode_iface_id = adapter->iface_id;
			RTW_INFO(ADPT_FMT ": DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				 ADPT_ARG(adapter), DLBcnCount, poll);
		}

		val8 = rtw_read8(adapter, REG_BCN_CTRL_8821C);
		val8 |= BIT_EN_BCN_FUNCTION_8821C;
		val8 &= ~BIT_DIS_TSF_UDT_8821C;
		rtw_write8(adapter, REG_BCN_CTRL_8821C, val8);
#if 0
		/*
		 * To make sure that if there exists an adapter which would like to send beacon.
		 * If exists, the origianl value of 0x422[6] will be 1, we should check this to
		 * prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
		 * the beacon cannot be sent by HW.
		 */
		if (bRecover) {
			rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8821C + 2, hal->RegFwHwTxQCtrl | BIT(6));
			hal->RegFwHwTxQCtrl |= BIT(6);
		}
#endif
#ifndef CONFIG_PCI_HCI
		/* Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
		v8 = rtw_read8(adapter, REG_CR_8821C + 1);
		v8 &= ~BIT(0); /* ~ENSWBCN */
		rtw_write8(adapter, REG_CR_8821C + 1, v8);
#endif /* !CONFIG_PCI_HCI */
	}

}

static void hw_var_set_h2c_fw_joinbssrpt(PADAPTER adapter, u8 mstatus)
{
	if (mstatus == RT_MEDIA_CONNECT)
		hw_var_set_dl_rsvd_page(adapter, RT_MEDIA_CONNECT);
}

/*
 * Description: Get the reserved page number in Tx packet buffer.
 * Retrun value: the page number.
 */
u8 get_txbuffer_rsvdpagenum(PADAPTER adapter, bool wowlan)
{
	u8 RsvdPageNum = HALMAC_RSVD_DRV_PGNUM_8821C;

#ifdef CONFIG_PNO_SUPPORT
	RsvdPageNum = 32;
#endif
	return RsvdPageNum;
}


/*
 * Parameters:
 *	adapter
 *	enable		_TRUE: enable; _FALSE: disable
 */
static u8 rx_agg_switch(PADAPTER adapter, u8 enable)
{
	/* if (rtl8821c_config_rx_agg(adapter, enable)) */
	return _SUCCESS;

	return _FAIL;
}

void rtl8821c_sethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 val8;
	u16 val16;
	u32 val32;


	switch (variable) {
	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(adapter, val);
		break;

	case HW_VAR_MAC_ADDR:
		hw_var_set_macaddr(adapter, val);
		break;

	case HW_VAR_BSSID:
		hw_var_set_bssid(adapter, val);
		break;
	/*
		case HW_VAR_INIT_RTS_RATE:
			break;
	*/
	case HW_VAR_BASIC_RATE:
		hw_var_set_basic_rate(adapter, val);
		break;

	case HW_VAR_TXPAUSE:
		rtw_write8(adapter, REG_TXPAUSE_8821C, *val);
		break;

	case HW_VAR_BCN_FUNC:
		hw_var_set_bcn_func(adapter, *val);
		break;

	case HW_VAR_CORRECT_TSF:
		hw_var_set_correct_tsf(adapter);
		break;

	case HW_VAR_CHECK_BSSID:
		hw_var_set_check_bssid(adapter, *val);
		break;

	case HW_VAR_MLME_DISCONNECT:
		hw_var_set_mlme_disconnect(adapter);
		break;

	case HW_VAR_MLME_SITESURVEY:
		hw_var_set_mlme_sitesurvey(adapter, *val);
#ifdef CONFIG_BT_COEXIST
		if (hal->EEPROMBluetoothCoexist)
			rtw_btcoex_ScanNotify(adapter, *val ? _TRUE : _FALSE);
		else
			rtw_btcoex_wifionly_scan_notify(adapter);
#else /* !CONFIG_BT_COEXIST */
		rtw_btcoex_wifionly_scan_notify(adapter);
#endif /* CONFIG_BT_COEXIST */

		break;

	case HW_VAR_MLME_JOIN:
		hw_var_set_mlme_join(adapter, *val);

#ifdef CONFIG_BT_COEXIST
		if (hal->EEPROMBluetoothCoexist) {
			switch (*val) {
			case 0:
				/* Notify coex. mechanism before join */
				rtw_btcoex_ConnectNotify(adapter, _TRUE);
				break;
			case 1:
			case 2:
				/* Notify coex. mechanism after join, whether successful or failed */
				rtw_btcoex_ConnectNotify(adapter, _FALSE);
				break;
			}
		}
#endif /* CONFIG_BT_COEXIST */
		break;

	case HW_VAR_ON_RCR_AM:
		hw_var_set_rcr_am(adapter, 1);
		break;

	case HW_VAR_OFF_RCR_AM:
		hw_var_set_rcr_am(adapter, 0);
		break;

	case HW_VAR_BEACON_INTERVAL:
		hw_var_set_bcn_interval(adapter, *(u16 *)val);
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
		{
			struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
			struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
			u16 bcn_interval =	*((u16 *)val);
			if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
				RTW_INFO("%s==> bcn_interval:%d, eraly_int:%d\n", __FUNCTION__, bcn_interval, bcn_interval >> 1);
				rtw_write8(adapter, REG_DRVERLYINT, bcn_interval >> 1); /* 50ms for sdio */
			}
		}
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */
		break;

	case HW_VAR_SLOT_TIME:
		rtw_write8(adapter, REG_SLOT_8821C, *val);
		break;

	case HW_VAR_RESP_SIFS:
		/* RESP_SIFS for CCK */
		rtw_write8(adapter, REG_RESP_SIFS_CCK_8821C, val[0]);
		rtw_write8(adapter, REG_RESP_SIFS_CCK_8821C + 1, val[1]);
		/* RESP_SIFS for OFDM */
		rtw_write8(adapter, REG_RESP_SIFS_OFDM_8821C, val[2]);
		rtw_write8(adapter, REG_RESP_SIFS_OFDM_8821C + 1, val[3]);
		break;

	case HW_VAR_ACK_PREAMBLE:
		hw_var_set_ack_preamble(adapter, *val);
		break;

	case HW_VAR_SEC_CFG:
		hw_var_set_sec_cfg(adapter, *val);
		break;

	case HW_VAR_SEC_DK_CFG:
		if (val)
			hw_var_set_sec_dk_cfg(adapter, _TRUE);
		else
			hw_var_set_sec_dk_cfg(adapter, _FALSE);
		break;

	case HW_VAR_BCN_VALID:
		hw_var_set_bcn_valid(adapter);
		break;
	/*
		case HW_VAR_RF_TYPE:
			break;
	*/
	case HW_VAR_CAM_EMPTY_ENTRY:
		hw_var_set_cam_empty_entry(adapter, *val);
		break;

	case HW_VAR_CAM_INVALID_ALL:
		val32 = BIT_SECCAM_POLLING_8821C | BIT_SECCAM_CLR_8821C;
		rtw_write32(adapter, REG_CAMCMD_8821C, val32);
		break;

	case HW_VAR_AC_PARAM_VO:
		rtw_write32(adapter, REG_EDCA_VO_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_VI:
		rtw_write32(adapter, REG_EDCA_VI_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_BE:
		hal->ac_param_be = *(u32 *)val;
		rtw_write32(adapter, REG_EDCA_BE_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_BK:
		rtw_write32(adapter, REG_EDCA_BK_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_ACM_CTRL:
		hw_var_set_acm_ctrl(adapter, *val);
		break;
	/*
		case HW_VAR_AMPDU_MIN_SPACE:
			break;
	*/
	case HW_VAR_AMPDU_FACTOR: {
		u32 AMPDULen = *val; /* enum AGGRE_SIZE */

		AMPDULen = (0x2000 << AMPDULen) - 1;
		rtw_write32(adapter, REG_AMPDU_MAX_LENGTH_8821C, AMPDULen);
	}
	break;

	case HW_VAR_RXDMA_AGG_PG_TH:
		/*
		 * TH=1 => invalidate RX DMA aggregation
		 * TH=0 => validate RX DMA aggregation, use init value.
		 */
		if (*val == 0)
			/* enable RXDMA aggregation */
			rx_agg_switch(adapter, _TRUE);
		else
			/* disable RXDMA aggregation */
			rx_agg_switch(adapter, _FALSE);
		break;

	case HW_VAR_H2C_FW_PWRMODE:
		rtl8821c_set_FwPwrMode_cmd(adapter, *val);
		break;
	/*
		case HW_VAR_H2C_PS_TUNE_PARAM:
			break;
	*/
	case HW_VAR_H2C_FW_JOINBSSRPT:
		hw_var_set_h2c_fw_joinbssrpt(adapter, *val);
		break;
		/*
			case HW_VAR_FWLPS_RF_ON:
				break;
		*/
#ifdef CONFIG_P2P
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtw_set_p2p_ps_offload_cmd(adapter, *val);
		break;
#endif

#ifdef CONFIG_TDLS
	case HW_VAR_TDLS_WRCR:
		rtl8821c_rcr_clear(adapter, BIT_CBSSID_DATA_8821C);
		break;

	case HW_VAR_TDLS_RS_RCR:
		rtl8821c_rcr_add(adapter, BIT_CBSSID_DATA_8821C);
		break;
#endif
		/*
			case HW_VAR_TRIGGER_GPIO_0:
			case HW_VAR_BT_SET_COEXIST:
			case HW_VAR_BT_ISSUE_DELBA:
			case HW_VAR_CURRENT_ANTENNA:
				break;
		*/
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	case HW_VAR_ANTENNA_DIVERSITY_SELECT:
		ODM_SetAntConfig(&hal->odmpriv, *val);
		break;
#endif
	/*
		case HW_VAR_SWITCH_EPHY_WoWLAN:
		case HW_VAR_EFUSE_USAGE:
		case HW_VAR_EFUSE_BYTES:
		case HW_VAR_EFUSE_BT_USAGE:
		case HW_VAR_EFUSE_BT_BYTES:
			break;
	*/
	case HW_VAR_FIFO_CLEARN_UP: {
		struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
		u8 trycnt = 100;
		u32 reg_hw_ssn;

		/* pause tx */
		rtw_write8(adapter, REG_TXPAUSE_8821C, 0xff);

		/* keep hw sn */
		if (adapter->xmitpriv.hw_ssn_seq_no == 1)
			reg_hw_ssn = REG_HW_SEQ1_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 2)
			reg_hw_ssn = REG_HW_SEQ2_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 3)
			reg_hw_ssn = REG_HW_SEQ3_8821C;
		else
			reg_hw_ssn = REG_HW_SEQ0_8821C;

		adapter->xmitpriv.nqos_ssn = rtw_read16(adapter, reg_hw_ssn);

		if (pwrpriv->bkeepfwalive != _TRUE) {
			/* RX DMA stop */
			val32 = rtw_read32(adapter, REG_RXPKT_NUM_8821C);
			val32 |= BIT_RW_RELEASE_EN;
			rtw_write32(adapter, REG_RXPKT_NUM_8821C, val32);
			do {
				val32 = rtw_read32(adapter, REG_RXPKT_NUM_8821C);
				val32 &= BIT_RXDMA_IDLE_8821C;
				if (val32)
					break;

				RTW_INFO("[HW_VAR_FIFO_CLEARN_UP] val=%x times:%d\n", val32, trycnt);
			} while (--trycnt);
			if (trycnt == 0)
				RTW_INFO("[HW_VAR_FIFO_CLEARN_UP] Stop RX DMA failed!\n");
#if 0
			/* RQPN Load 0 */
			rtw_write16(adapter, REG_RQPN_NPQ, 0);
			rtw_write32(adapter, REG_RQPN, 0x80000000);
			rtw_mdelay_os(2);
#endif
		}
	}
	break;

	case HW_VAR_RESTORE_HW_SEQ: {
		/* restore Sequence No. */
		u32 reg_hw_ssn;

		if (adapter->xmitpriv.hw_ssn_seq_no == 1)
			reg_hw_ssn = REG_HW_SEQ1_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 2)
			reg_hw_ssn = REG_HW_SEQ2_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 3)
			reg_hw_ssn = REG_HW_SEQ3_8821C;
		else
			reg_hw_ssn = REG_HW_SEQ0_8821C;

		rtw_write8(adapter, reg_hw_ssn, adapter->xmitpriv.nqos_ssn);
	}
	break;

	case HW_VAR_CHECK_TXBUF: {
		u16 rtylmtorg;
		u8 RetryLimit = 0x01;
		systime start;
		u32 passtime;
		u32 timelmt = 2000;	/* ms */
		u32 waittime = 10;	/* ms */
		u32 high, low, normal, extra, publc;
		u16 rsvd, available;
		u8 empty;


		rtylmtorg = rtw_read16(adapter, REG_RETRY_LIMIT_8821C);

		val16 = BIT_LRL_8821C(RetryLimit) | BIT_SRL_8821C(RetryLimit);
		rtw_write16(adapter, REG_RETRY_LIMIT_8821C, val16);

		/* Check TX FIFO empty or not */
		empty = _FALSE;
		high = 0;
		low = 0;
		normal = 0;
		extra = 0;
		publc = 0;
		start = rtw_get_current_time();
		while ((rtw_get_passing_time_ms(start) < timelmt)
		       && !RTW_CANNOT_RUN(adapter)) {
			high = rtw_read32(adapter, REG_FIFOPAGE_INFO_1_8821C);
			low = rtw_read32(adapter, REG_FIFOPAGE_INFO_2_8821C);
			normal = rtw_read32(adapter, REG_FIFOPAGE_INFO_3_8821C);
			extra = rtw_read32(adapter, REG_FIFOPAGE_INFO_4_8821C);
			publc = rtw_read32(adapter, REG_FIFOPAGE_INFO_5_8821C);

			rsvd = BIT_GET_HPQ_V1_8821C(high);
			available = BIT_GET_HPQ_AVAL_PG_V1_8821C(high);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_LPQ_V1_8821C(low);
			available = BIT_GET_LPQ_AVAL_PG_V1_8821C(low);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_NPQ_V1_8821C(normal);
			available = BIT_GET_NPQ_AVAL_PG_V1_8821C(normal);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_EXQ_V1_8821C(extra);
			available = BIT_GET_EXQ_AVAL_PG_V1_8821C(extra);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_PUBQ_V1_8821C(publc);
			available = BIT_GET_PUBQ_AVAL_PG_V1_8821C(publc);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			empty = _TRUE;
			break;
		}

		passtime = rtw_get_passing_time_ms(start);
		if (_TRUE == empty)
			RTW_INFO("[HW_VAR_CHECK_TXBUF] Empty in %d ms\n", passtime);
		else if (RTW_CANNOT_RUN(adapter))
			RTW_INFO("[HW_VAR_CHECK_TXBUF] bDriverStopped or bSurpriseRemoved\n");
		else {
			RTW_INFO("[HW_VAR_CHECK_TXBUF] NOT empty in %d ms\n", passtime);
			RTW_INFO("[HW_VAR_CHECK_TXBUF] 0x230=0x%08x 0x234=0x%08x 0x238=0x%08x 0x23c=0x%08x 0x240=0x%08x\n",
				 high, low, normal, extra, publc);
		}

		rtw_write16(adapter, REG_RETRY_LIMIT_8821C, rtylmtorg);
	}
	break;
	/*
		case HW_VAR_PCIE_STOP_TX_DMA:
			break;
	*/

	/*
		case HW_VAR_SYS_CLKR:
			break;
	*/
	case HW_VAR_NAV_UPPER: {
#define HAL_NAV_UPPER_UNIT	128	/* micro-second */
		u32 usNavUpper = *(u32 *)val;

		if (usNavUpper > HAL_NAV_UPPER_UNIT * 0xFF) {
			RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_NAV_UPPER] value(0x%08X us) is larger than (%d * 0xFF)!!!\n",
				FUNC_ADPT_ARG(adapter), usNavUpper, HAL_NAV_UPPER_UNIT);
			break;
		}

		usNavUpper = (usNavUpper + HAL_NAV_UPPER_UNIT - 1) / HAL_NAV_UPPER_UNIT;
		rtw_write8(adapter, REG_NAV_CTRL_8821C + 2, (u8)usNavUpper);
	}
	break;

	/*
		case HW_VAR_RPT_TIMER_SETTING:
		case HW_VAR_TX_RPT_MAX_MACID:
		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			break;
	*/
	case HW_VAR_DL_BCN_SEL:
#ifdef CONFIG_CONCURRENT_MODE
		if (adapter->hw_port == HW_PORT1) {
			/* Port1 */
			/* ToDo */
		} else
#endif /* CONFIG_CONCURRENT_MODE */
		{
			/* Port0 */
			/* ToDo */
		}
		break;
	/*
		case HW_VAR_AMPDU_MAX_TIME:
		case HW_VAR_WIRELESS_MODE:
		case HW_VAR_USB_MODE:
		break;
	*/
#ifdef CONFIG_AP_PORT_SWAP
	case HW_VAR_PORT_SWITCH:
		{
			u8 mode = *((u8 *)val);

			rtl8821c_ap_port_switch(adapter, mode);
		}
		break;
#endif
	case HW_VAR_DO_IQK:
		if (*val)
			hal->bNeedIQK = _TRUE;
		else
			hal->bNeedIQK = _FALSE;
		break;

	case HW_VAR_DM_IN_LPS:
		rtl8821c_phy_haldm_in_lps(adapter);
		break;
	/*
		case HW_VAR_SET_REQ_FW_PS:
		case HW_VAR_FW_PS_STATE:
		case HW_VAR_SOUNDING_ENTER:
		case HW_VAR_SOUNDING_LEAVE:
		case HW_VAR_SOUNDING_RATE:
		case HW_VAR_SOUNDING_STATUS:
		case HW_VAR_SOUNDING_FW_NDPA:
		case HW_VAR_SOUNDING_CLK:
			break;
	*/
	case HW_VAR_DL_RSVD_PAGE:
#ifdef CONFIG_BT_COEXIST
		if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			rtl8821c_download_BTCoex_AP_mode_rsvd_page(adapter);
		else
#endif
		{
			hw_var_set_dl_rsvd_page(adapter, RT_MEDIA_CONNECT);
		}
		break;

	case HW_VAR_MACID_SLEEP: {
		u32 reg_macid_sleep;
		u8 bit_shift;
		u8 id = *(u8 *)val;

		if (id < 32) {
			reg_macid_sleep = REG_MACID_SLEEP_8821C;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_sleep = REG_MACID_SLEEP1_8821C;
			bit_shift = id - 32;
		} else if (id < 96) {
			reg_macid_sleep = REG_MACID_SLEEP2_8821C;
			bit_shift = id - 64;
		} else if (id < 128) {
			reg_macid_sleep = REG_MACID_SLEEP3_8821C;
			bit_shift = id - 96;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_sleep);
		RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_MACID_SLEEP] macid=%d, org reg_0x%03x=0x%08X\n",
			FUNC_ADPT_ARG(adapter), id, reg_macid_sleep, val32);

		if (val32 & BIT(bit_shift))
			break;

		val32 |= BIT(bit_shift);
		rtw_write32(adapter, reg_macid_sleep, val32);
	}
	break;

	case HW_VAR_MACID_WAKEUP: {
		u32 reg_macid_sleep;
		u8 bit_shift;
		u8 id = *(u8 *)val;

		if (id < 32) {
			reg_macid_sleep = REG_MACID_SLEEP_8821C;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_sleep = REG_MACID_SLEEP1_8821C;
			bit_shift = id - 32;
		} else if (id < 96) {
			reg_macid_sleep = REG_MACID_SLEEP2_8821C;
			bit_shift = id - 64;
		} else if (id < 128) {
			reg_macid_sleep = REG_MACID_SLEEP3_8821C;
			bit_shift = id - 96;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_sleep);
		RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_MACID_WAKEUP] macid=%d, org reg_0x%03x=0x%08X\n",
			FUNC_ADPT_ARG(adapter), id, reg_macid_sleep, val32);

		if (!(val32 & BIT(bit_shift)))
			break;

		val32 &= ~BIT(bit_shift);
		rtw_write32(adapter, reg_macid_sleep, val32);
	}
	break;

	default:
		SetHwReg(adapter, variable, val);
		break;
	}

}

struct qinfo {
	u32 head:8;
	u32 pkt_num:7;
	u32 tail:8;
	u32 ac:2;
	u32 macid:7;
};

struct bcn_qinfo {
	u16 head:8;
	u16 pkt_num:8;
};

static void dump_qinfo(void *sel, struct qinfo *info, const char *tag)
{
	RTW_PRINT_SEL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n",
		tag ? tag : "", info->head, info->tail, info->pkt_num, info->macid, info->ac);
}

static void dump_bcn_qinfo(void *sel, struct bcn_qinfo *info, const char *tag)
{
	RTW_PRINT_SEL(sel, "%shead:0x%02x, pkt_num:%u\n",
		      tag ? tag : "", info->head, info->pkt_num);
}

static void dump_mac_qinfo(void *sel, _adapter *adapter)
{
	u32 q0_info;
	u32 q1_info;
	u32 q2_info;
	u32 q3_info;
	u32 q4_info;
	u32 q5_info;
	u32 q6_info;
	u32 q7_info;
	u32 mg_q_info;
	u32 hi_q_info;
	u16 bcn_q_info;

	q0_info = rtw_read32(adapter, REG_Q0_INFO_8821C);
	q1_info = rtw_read32(adapter, REG_Q1_INFO_8821C);
	q2_info = rtw_read32(adapter, REG_Q2_INFO_8821C);
	q3_info = rtw_read32(adapter, REG_Q3_INFO_8821C);
	q4_info = rtw_read32(adapter, REG_Q4_INFO_8821C);
	q5_info = rtw_read32(adapter, REG_Q5_INFO_8821C);
	q6_info = rtw_read32(adapter, REG_Q6_INFO_8821C);
	q7_info = rtw_read32(adapter, REG_Q7_INFO_8821C);
	mg_q_info = rtw_read32(adapter, REG_MGQ_INFO_8821C);
	hi_q_info = rtw_read32(adapter, REG_HIQ_INFO_8821C);
	bcn_q_info = rtw_read16(adapter, REG_BCNQ_INFO_8821C);

	dump_qinfo(sel, (struct qinfo *)&q0_info, "Q0 ");
	dump_qinfo(sel, (struct qinfo *)&q1_info, "Q1 ");
	dump_qinfo(sel, (struct qinfo *)&q2_info, "Q2 ");
	dump_qinfo(sel, (struct qinfo *)&q3_info, "Q3 ");
	dump_qinfo(sel, (struct qinfo *)&q4_info, "Q4 ");
	dump_qinfo(sel, (struct qinfo *)&q5_info, "Q5 ");
	dump_qinfo(sel, (struct qinfo *)&q6_info, "Q6 ");
	dump_qinfo(sel, (struct qinfo *)&q7_info, "Q7 ");
	dump_qinfo(sel, (struct qinfo *)&mg_q_info, "MG ");
	dump_qinfo(sel, (struct qinfo *)&hi_q_info, "HI ");
	dump_bcn_qinfo(sel, (struct bcn_qinfo *)&bcn_q_info, "BCN ");
}

static u8 hw_var_get_bcn_valid(PADAPTER adapter)
{
	u8 val8 = 0;
	u8 ret = _FALSE;

	/* only port 0 can TX BCN */
	val8 = rtw_read8(adapter, REG_FIFOPAGE_CTRL_2_8821C + 1);
	ret = (BIT(7) & val8) ? _TRUE : _FALSE;

	return ret;
}

void rtl8821c_gethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal;
	u8 val8;
	u16 val16;
	u32 val32;


	hal = GET_HAL_DATA(adapter);

	switch (variable) {
	/*
		case HW_VAR_SET_OPMODE:
		case HW_VAR_MAC_ADDR:
		case HW_VAR_BSSID:
		case HW_VAR_INIT_RTS_RATE:
		case HW_VAR_BASIC_RATE:
			break;
	*/
	case HW_VAR_TXPAUSE:
		*val = rtw_read8(adapter, REG_TXPAUSE_8821C);
		break;
	/*
		case HW_VAR_BCN_FUNC:
		case HW_VAR_CORRECT_TSF:
		case HW_VAR_CHECK_BSSID:
		case HW_VAR_MLME_DISCONNECT:
		case HW_VAR_MLME_SITESURVEY:
		case HW_VAR_MLME_JOIN:
		case HW_VAR_ON_RCR_AM:
		case HW_VAR_OFF_RCR_AM:
		case HW_VAR_BEACON_INTERVAL:
		case HW_VAR_SLOT_TIME:
		case HW_VAR_RESP_SIFS:
		case HW_VAR_ACK_PREAMBLE:
		case HW_VAR_SEC_CFG:
		case HW_VAR_SEC_DK_CFG:
			break;
	*/
	case HW_VAR_BCN_VALID:
		*val = hw_var_get_bcn_valid(adapter);
		break;
	/*
		case HW_VAR_RF_TYPE:
		case HW_VAR_CAM_EMPTY_ENTRY:
		case HW_VAR_CAM_INVALID_ALL:
		case HW_VAR_AC_PARAM_VO:
		case HW_VAR_AC_PARAM_VI:
		case HW_VAR_AC_PARAM_BE:
		case HW_VAR_AC_PARAM_BK:
		case HW_VAR_ACM_CTRL:
		case HW_VAR_AMPDU_MIN_SPACE:
		case HW_VAR_AMPDU_FACTOR:
		case HW_VAR_RXDMA_AGG_PG_TH:
		case HW_VAR_H2C_FW_PWRMODE:
		case HW_VAR_H2C_PS_TUNE_PARAM:
		case HW_VAR_H2C_FW_JOINBSSRPT:
			break;
	*/
	case HW_VAR_FWLPS_RF_ON:
		/* When we halt NIC, we should check if FW LPS is leave. */
		if (rtw_is_surprise_removed(adapter) ||
		    (adapter_to_pwrctl(adapter)->rf_pwrstate == rf_off)) {
			/*
			 * If it is in HW/SW Radio OFF or IPS state,
			 * we do not check Fw LPS Leave,
			 * because Fw is unload.
			 */
			*val = _TRUE;
		} else {
			rtl8821c_rcr_get(adapter, &val32);
			val32 &= (BIT_UC_MD_EN_8821C | BIT_BC_MD_EN_8821C | BIT_TIM_PARSER_EN_8821C);
			if (val32)
				*val = _FALSE;
			else
				*val = _TRUE;
		}
		break;
		/*
			case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			case HW_VAR_TDLS_WRCR:
			case HW_VAR_TDLS_RS_RCR:
			case HW_VAR_TRIGGER_GPIO_0:
			case HW_VAR_BT_SET_COEXIST:
			case HW_VAR_BT_ISSUE_DELBA:
				break;
		*/
#ifdef CONFIG_ANTENNA_DIVERSITY
	case HW_VAR_CURRENT_ANTENNA:
		*val = hal->CurAntenna;
		break;
#endif
	/*
		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
		case HW_VAR_SWITCH_EPHY_WoWLAN:
		case HW_VAR_EFUSE_USAGE:
		case HW_VAR_EFUSE_BYTES:
		case HW_VAR_EFUSE_BT_USAGE:
		case HW_VAR_EFUSE_BT_BYTES:
		case HW_VAR_FIFO_CLEARN_UP:
		case HW_VAR_RESTORE_HW_SEQ:
		case HW_VAR_CHECK_TXBUF:
		case HW_VAR_PCIE_STOP_TX_DMA:
			break;
	*/

		/*
			case HW_VAR_HCI_SUS_STATE:
				break;
		*/


#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	case HW_VAR_SYS_CLKR:
		*val = rtw_read8(adapter, REG_SYS_CLK_CTRL_8821C);
		break;
#endif
	/*
		case HW_VAR_NAV_UPPER:
		case HW_VAR_RPT_TIMER_SETTING:
		case HW_VAR_TX_RPT_MAX_MACID:
			break;
	*/
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		val16 = rtw_read16(adapter, REG_TXPKT_EMPTY_8821C);
		*val = (val16 & BIT_HQQ_EMPTY_8821C) ? _TRUE : _FALSE;
		break;
	/*
		case HW_VAR_DL_BCN_SEL:
		case HW_VAR_AMPDU_MAX_TIME:
		case HW_VAR_WIRELESS_MODE:
		case HW_VAR_USB_MODE:
		case HW_VAR_DO_IQK:
		case HW_VAR_DM_IN_LPS:
		case HW_VAR_SET_REQ_FW_PS:
		case HW_VAR_FW_PS_STATE:
		case HW_VAR_SOUNDING_ENTER:
		case HW_VAR_SOUNDING_LEAVE:
		case HW_VAR_SOUNDING_RATE:
		case HW_VAR_SOUNDING_STATUS:
		case HW_VAR_SOUNDING_FW_NDPA:
		case HW_VAR_SOUNDING_CLK:
		case HW_VAR_DL_RSVD_PAGE:
		case HW_VAR_MACID_SLEEP:
		case HW_VAR_MACID_WAKEUP:
			break;
	*/
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
		dump_mac_qinfo(val, adapter);
		break;
	/*
		case HW_VAR_ASIX_IOT:
		case HW_VAR_H2C_BT_MP_OPER:
			break;
	*/
	default:
		GetHwReg(adapter, variable, val);
		break;
	}
}

/*
 * Description:
 *	Change default setting of specified variable.
 */
u8 rtl8821c_sethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	u8 bResult;


	hal = GET_HAL_DATA(adapter);
	bResult = _SUCCESS;

	switch (variable) {
	/*
		case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
		case HAL_DEF_IS_SUPPORT_ANT_DIV:
		case HAL_DEF_DRVINFO_SZ:
		case HAL_DEF_MAX_RECVBUF_SZ:
		case HAL_DEF_RX_PACKET_OFFSET:
		case HAL_DEF_RX_DMA_SZ_WOW:
		case HAL_DEF_RX_DMA_SZ:
		case HAL_DEF_RX_PAGE_SIZE:
		case HAL_DEF_DBG_DUMP_RXPKT:
		case HAL_DEF_RA_DECISION_RATE:
		case HAL_DEF_RA_SGI:
		case HAL_DEF_PT_PWR_STATUS:
		case HW_VAR_MAX_RX_AMPDU_FACTOR:
		case HW_DEF_RA_INFO_DUMP:
		case HAL_DEF_DBG_DUMP_TXPKT:
		case HAL_DEF_TX_PAGE_BOUNDARY:
		case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
		case HAL_DEF_ANT_DETECT:
		case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
		case HAL_DEF_PCI_AMD_L1_SUPPORT:
		case HAL_DEF_PCI_ASPM_OSC:
		case HAL_DEF_MACID_SLEEP:
		case HAL_DEF_DBG_DIS_PWT:
		case HAL_DEF_EFUSE_USAGE:
		case HAL_DEF_EFUSE_BYTES:
		case HW_VAR_BEST_AMPDU_DENSITY:
			break;
	*/
	default:
		bResult = SetHalDefVar(adapter, variable, pval);
		break;
	}

	return bResult;
}

/*
 * Description:
 *	Query setting of specified variable.
 */
u8 rtl8821c_gethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	u8 bResult;
	u8 val8 = 0;


	hal = GET_HAL_DATA(adapter);
	bResult = _SUCCESS;

	switch (variable) {
	/*
		case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
			break;
	*/
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
		*(u8 *)pval = _TRUE;
#else
		*(u8 *)pval = _FALSE;
#endif
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pval) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		rtw_halmac_get_drv_info_sz(adapter_to_dvobj(adapter), &val8);
		*((u32 *)pval) = HALMAC_RX_DESC_SIZE_8821C + val8;
		break;
	/*
	case HAL_DEF_DRVINFO_SZ:
		rtw_halmac_get_drv_info_sz(adapter_to_dvobj(adapter), &val8);
		*((u32 *)pval) = val8;
		break;
		case HAL_DEF_RX_DMA_SZ_WOW:
		case HAL_DEF_RX_DMA_SZ:
		case HAL_DEF_RX_PAGE_SIZE:
		case HAL_DEF_DBG_DUMP_RXPKT:
		case HAL_DEF_RA_DECISION_RATE:
		case HAL_DEF_RA_SGI:
			break;
	*/
	/* only for 8188E */
	case HAL_DEF_PT_PWR_STATUS:
		break;

	case HAL_DEF_TX_LDPC:
		*(u8 *)pval = ((hal->phy_spec.ldpc_cap >> 8) & 0xFF) ? _TRUE : _FALSE;
		break;

	case HAL_DEF_RX_LDPC:
		*(u8 *)pval = (hal->phy_spec.ldpc_cap & 0xFF) ? _TRUE : _FALSE;
		break;

	case HAL_DEF_TX_STBC:
		*(u8 *)pval = ((hal->phy_spec.stbc_cap >> 8) & 0xFF) ? _TRUE : _FALSE;
		break;

	/* support 1RX for STBC */
	case HAL_DEF_RX_STBC:
		*(u8 *)pval = hal->phy_spec.stbc_cap & 0xFF;
		break;

	/* support Explicit TxBF for HT/VHT */
	case HAL_DEF_EXPLICIT_BEAMFORMER:
		*(u8 *)pval = _FALSE;
		break;

	case HAL_DEF_EXPLICIT_BEAMFORMEE:
#ifdef CONFIG_BEAMFORMING
		*(u8 *)pval = _TRUE;
#else
		*(u8 *)pval = _FALSE;
#endif
		break;
	/*
		case HAL_DEF_BEAMFORMER_CAP:
		case HAL_DEF_BEAMFORMEE_CAP:
			break;
	*/

	case HW_DEF_RA_INFO_DUMP:
#if 0
		{
			u8 mac_id = *(u8 *)pval;
			u32 cmd;
			u32 ra_info1, ra_info2;
			u32 rate_mask1, rate_mask2;
			u8 curr_tx_rate, curr_tx_sgi, hight_rate, lowest_rate;

			RTW_INFO("============ RA status check  Mac_id:%d ===================\n", mac_id);

			cmd = 0x40000100 | mac_id;
			rtw_write32(adapter, REG_HMEBOX_DBG_2_8723B, cmd);
			rtw_msleep_os(10);
			ra_info1 = rtw_read32(adapter, 0x2F0);
			curr_tx_rate = ra_info1 & 0x7F;
			curr_tx_sgi = (ra_info1 >> 7) & 0x01;
			RTW_INFO("[ ra_info1:0x%08x ] =>cur_tx_rate=%s, cur_sgi:%d, PWRSTS=0x%02x\n",
				 ra_info1,
				 HDATA_RATE(curr_tx_rate),
				 curr_tx_sgi,
				 (ra_info1 >> 8) & 0x07);

			cmd = 0x40000400 | mac_id;
			rtw_write32(adapter, REG_HMEBOX_DBG_2_8723B, cmd);
			rtw_msleep_os(10);
			ra_info1 = rtw_read32(adapter, 0x2F0);
			ra_info2 = rtw_read32(adapter, 0x2F4);
			rate_mask1 = rtw_read32(adapter, 0x2F8);
			rate_mask2 = rtw_read32(adapter, 0x2FC);
			hight_rate = ra_info2 & 0xFF;
			lowest_rate = (ra_info2 >> 8)  & 0xFF;

			RTW_INFO("[ ra_info1:0x%08x ] =>RSSI=%d, BW_setting=0x%02x, DISRA=0x%02x, VHT_EN=0x%02x\n",
				 ra_info1,
				 ra_info1 & 0xFF,
				 (ra_info1 >> 8)  & 0xFF,
				 (ra_info1 >> 16) & 0xFF,
				 (ra_info1 >> 24) & 0xFF);

			RTW_INFO("[ ra_info2:0x%08x ] =>hight_rate=%s, lowest_rate=%s, SGI=0x%02x, RateID=%d\n",
				 ra_info2,
				 HDATA_RATE(hight_rate),
				 HDATA_RATE(lowest_rate),
				 (ra_info2 >> 16) & 0xFF,
				 (ra_info2 >> 24) & 0xFF);

			RTW_INFO("rate_mask2=0x%08x, rate_mask1=0x%08x\n", rate_mask2, rate_mask1);
		}
#endif
		break;
	/*
		case HAL_DEF_DBG_DUMP_TXPKT:
			break;
	*/
	case HAL_DEF_TX_PAGE_SIZE:
		*(u32 *)pval = HALMAC_TX_PAGE_SIZE_8821C;
		break;
	case HAL_DEF_TX_PAGE_BOUNDARY:
		rtw_halmac_get_rsvd_drv_pg_bndy(adapter_to_dvobj(adapter), (u16 *)pval);
		break;
	/*	case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
		case HAL_DEF_ANT_DETECT:
		case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
		case HAL_DEF_PCI_AMD_L1_SUPPORT:
		case HAL_DEF_PCI_ASPM_OSC:
			break;
	*/
	case HAL_DEF_MACID_SLEEP:
		*(u8 *)pval = _TRUE; /* support macid sleep */
		break;
	/*
		case HAL_DEF_DBG_DIS_PWT:
		case HAL_DEF_EFUSE_USAGE:
		case HAL_DEF_EFUSE_BYTES:
			break;
	*/
	case HW_VAR_BEST_AMPDU_DENSITY:
		*((u32 *)pval) = AMPDU_DENSITY_VALUE_4;
		break;

	default:
		bResult = GetHalDefVar(adapter, variable, pval);
		break;
	}

	return bResult;
}

/* xmit section */
void rtl8821c_init_xmit_priv(_adapter *adapter)
{
	struct xmit_priv *pxmitpriv = &adapter->xmitpriv;

	pxmitpriv->hw_ssn_seq_no = rtw_get_hwseq_no(adapter);
	pxmitpriv->nqos_ssn = 0;
}


#if defined(CONFIG_CONCURRENT_MODE)
void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if ((pattrib->encrypt > 0) && (!pattrib->bswenc)
	    && (pattrib->bmc_camid != INVALID_SEC_MAC_CAM_ID)) {

		SET_TX_DESC_EN_DESC_ID_8821C(ptxdesc, 1);
		SET_TX_DESC_MACID_8821C(ptxdesc, pattrib->bmc_camid);
	}
}
#endif

void rtl8821c_fill_txdesc_sectype(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
		/* SEC_TYPE : 0:NO_ENC,1:WEP40/TKIP,2:WAPI,3:AES */
		switch (pattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
		case _TKIP_WTMIC_:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x1);
			break;
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x2);
			break;
#endif
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x3);
			break;
		case _NO_PRIVACY_:
		default:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x0);
			break;
		}
	}
}

void rtl8821c_fill_txdesc_vcs(PADAPTER adapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);


	if (pattrib->vcs_mode) {
		switch (pattrib->vcs_mode) {
		case RTS_CTS:
			SET_TX_DESC_RTSEN_8821C(ptxdesc, 1);
			break;
		case CTS_TO_SELF:
			SET_TX_DESC_CTS2SELF_8821C(ptxdesc, 1);
			break;
		case NONE_VCS:
		default:
			break;
		}

		if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
			SET_TX_DESC_RTS_SHORT_8821C(ptxdesc, 1);

		SET_TX_DESC_RTSRATE_8821C(ptxdesc, 0x8);/* RTS Rate=24M */
		SET_TX_DESC_RTS_RTY_LOWEST_RATE_8821C(ptxdesc, 0xf);
	}
}

u8 rtl8821c_bw_mapping(PADAPTER adapter, struct pkt_attrib *pattrib)
{
	u8 BWSettingOfDesc = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (hal->current_channel_bw == CHANNEL_WIDTH_80) {
		if (pattrib->bwmode == CHANNEL_WIDTH_80)
			BWSettingOfDesc = 2;
		else if (pattrib->bwmode == CHANNEL_WIDTH_40)
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else if (hal->current_channel_bw == CHANNEL_WIDTH_40) {
		if ((pattrib->bwmode == CHANNEL_WIDTH_40) || (pattrib->bwmode == CHANNEL_WIDTH_80))
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else
		BWSettingOfDesc = 0;

	return BWSettingOfDesc;
}

u8 rtl8821c_sc_mapping(PADAPTER adapter, struct pkt_attrib *pattrib)
{
	u8 SCSettingOfDesc = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (hal->current_channel_bw == CHANNEL_WIDTH_80) {
		if (pattrib->bwmode == CHANNEL_WIDTH_80)
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		else if (pattrib->bwmode == CHANNEL_WIDTH_40) {
			if (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				SCSettingOfDesc = VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				SCSettingOfDesc = VHT_DATA_SC_40_UPPER_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		} else {
			if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
			else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		}
	} else if (hal->current_channel_bw == CHANNEL_WIDTH_40) {
		if (pattrib->bwmode == CHANNEL_WIDTH_40)
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		else if (pattrib->bwmode == CHANNEL_WIDTH_20) {
			if (hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if (hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else
				SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		}
	} else
		SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;

	return SCSettingOfDesc;
}

void rtl8821c_fill_txdesc_phy(PADAPTER adapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if (pattrib->ht_en) {
		/* Set Bandwidth and sub-channel settings. */
		SET_TX_DESC_DATA_BW_8821C(ptxdesc, rtl8821c_bw_mapping(adapter, pattrib));
		SET_TX_DESC_DATA_SC_8821C(ptxdesc, rtl8821c_sc_mapping(adapter, pattrib));
	}
}

void rtl8821c_cal_txdesc_chksum(PADAPTER adapter, u8 *ptxdesc)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;


	halmac = adapter_to_halmac(adapter);
	api = HALMAC_GET_API(halmac);

	api->halmac_fill_txdesc_checksum(halmac, ptxdesc);
}


#ifdef CONFIG_MP_INCLUDED
void rtl8821c_prepare_mp_txdesc(PADAPTER adapter, struct mp_priv *pmp_priv)
{
	u8 *desc;
	struct pkt_attrib *attrib;
	u32 pkt_size;
	s32 bmcast;
	u8 data_rate, pwr_status, offset;


	desc = pmp_priv->tx.desc;
	attrib = &pmp_priv->tx.attrib;
	pkt_size = attrib->last_txcmdsz;
	bmcast = IS_MCAST(attrib->ra);

	SET_TX_DESC_LS_8821C(desc, 1);
	SET_TX_DESC_TXPKTSIZE_8821C(desc, pkt_size);

	offset = HALMAC_TX_DESC_SIZE_8821C;
	SET_TX_DESC_OFFSET_8821C(desc, offset);

#if defined(CONFIG_PCI_HCI) || defined(CONFIG_SDIO_HCI)
	SET_TX_DESC_PKT_OFFSET_8821C(desc, 0); /* Don't need to set PACKET Offset bit,it's no use 512bytes of length */
#else
	SET_TX_DESC_PKT_OFFSET_8821C(desc, 1);
#endif

	if (bmcast)
		SET_TX_DESC_BMC_8821C(desc, 1);

	SET_TX_DESC_MACID_8821C(desc, attrib->mac_id);
	SET_TX_DESC_RATE_ID_8821C(desc, attrib->raid);
	SET_TX_DESC_QSEL_8821C(desc, attrib->qsel);

	if (pmp_priv->preamble)
		SET_TX_DESC_DATA_SHORT_8821C(desc, 1);

	if (!attrib->qos_en)
		SET_TX_DESC_EN_HWSEQ_8821C(desc, 1);
	else
		SET_TX_DESC_SW_SEQ_8821C(desc, attrib->seqnum);

	if (pmp_priv->bandwidth <= CHANNEL_WIDTH_160)
		SET_TX_DESC_DATA_BW_8821C(desc, pmp_priv->bandwidth);
	else {
		RTW_INFO("%s: <ERROR> unknown bandwidth %d, use 20M\n",
			 __FUNCTION__, pmp_priv->bandwidth);
		SET_TX_DESC_DATA_BW_8821C(desc, CHANNEL_WIDTH_20);
	}

	SET_TX_DESC_DISDATAFB_8821C(desc, 1);
	SET_TX_DESC_USE_RATE_8821C(desc, 1);
	SET_TX_DESC_DATARATE_8821C(desc, pmp_priv->rateidx);
}
#endif /* CONFIG_MP_INCLUDED */

#define OFFSET_SZ	0
static void fill_default_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	PADAPTER adapter = pxmitframe->padapter;
	HAL_DATA_TYPE *phal_data = GET_HAL_DATA(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = IS_MCAST(pattrib->ra);
	u8 offset, pkt_offset = 0;

#define RA_SW_DEFINE_CONT	0x01
	u8	drv_fixed_reate = _FALSE;

#if 0
#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	if (adapter->registrypriv.mp_mode == 0) {
		if ((PACKET_OFFSET_SZ != 0) && (!bagg_pkt) &&
		    (rtw_usb_bulk_size_boundary(adapter, TXDESC_SIZE + pattrib->last_txcmdsz) == _FALSE)) {
			ptxdesc = (pmem + PACKET_OFFSET_SZ);
			/* RTW_INFO("==> non-agg-pkt,shift pointer...\n"); */
			pull = 1;
		}
	}
#endif	/* CONFIG_USE_USB_BUFFER_ALLOC_TX*/
#endif
	_rtw_memset(pbuf, 0, HALMAC_TX_DESC_SIZE_8821C);

	/*SET_TX_DESC_LS_8821C(pbuf, 1);*/ /*for USB only*/

	SET_TX_DESC_TXPKTSIZE_8821C(pbuf, pattrib->last_txcmdsz);

	offset = HALMAC_TX_DESC_SIZE_8821C + OFFSET_SZ;

#ifdef CONFIG_TX_EARLY_MODE
	if (pxmitframe->frame_tag == DATA_FRAMETAG)
		if (bagg_pkt)
			offset += EARLY_MODE_INFO_SIZE ;/*0x28	*/
	pkt_offset = pxmitframe->pkt_offset = 0x01;
#endif
	SET_TX_DESC_OFFSET_8821C(pbuf, offset);

	if (bmcst)
		SET_TX_DESC_BMC_8821C(pbuf, 1);

	SET_TX_DESC_MULTIPLE_PORT_8821C(pbuf, adapter->hw_port);

#if 0
#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	if (adapter->registrypriv.mp_mode == 0) {
		if ((PACKET_OFFSET_SZ != 0) && (!bagg_pkt)) {
			if ((pull) && (pxmitframe->pkt_offset > 0)) {
				pxmitframe->pkt_offset = pxmitframe->pkt_offset - 1;
				pkt_offset = pxmitframe->pkt_offset;
			}
		}
	}
	/*RTW_INFO("%s, pkt_offset=0x%02x\n", __func__, pxmitframe->pkt_offset);*/
#endif
#endif

	/* pkt_offset, unit:8 bytes padding*/
	if (pkt_offset > 0)
		SET_TX_DESC_PKT_OFFSET_8821C(pbuf, pkt_offset);


	SET_TX_DESC_MACID_8821C(pbuf, pattrib->mac_id);
	SET_TX_DESC_RATE_ID_8821C(pbuf, pattrib->raid);
	SET_TX_DESC_QSEL_8821C(pbuf, pattrib->qsel);

	/* 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS.
	* (1) The sequence number of each non-Qos frame / broadcast / multicast /
	* mgnt frame should be controled by Hw because Fw will also send null data
	* which we cannot control when Fw LPS enable.
	* --> default enable non-Qos data sequense number. 2010.06.23. by tynli.
	* (2) Enable HW SEQ control for beacon packet, because we use Hw beacon.
	* (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets.
	*/

	/* HW sequence, to fix to use 0 queue. todo: 4AC packets to use auto queue select */
	if (!pattrib->qos_en) {
		SET_TX_DESC_DISQSELSEQ_8821C(pbuf, 1);
		SET_TX_DESC_EN_HWSEQ_8821C(pbuf, 1);
		SET_TX_DESC_HW_SSN_SEL_8821C(pbuf, pattrib->hw_ssn_sel);
		SET_TX_DESC_EN_HWEXSEQ_8821C(pbuf, 0);
	} else
		SET_TX_DESC_SW_SEQ_8821C(pbuf, pattrib->seqnum);

	if (pxmitframe->frame_tag == DATA_FRAMETAG) {

		rtl8821c_fill_txdesc_sectype(pattrib, pbuf);

#if defined(CONFIG_CONCURRENT_MODE)
		if (bmcst)
			fill_txdesc_force_bmc_camid(pattrib, pbuf);
#endif

#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		if (pxmitframe->agg_num > 1) {
			/*RTW_INFO("%s agg_num:%d\n",__func__,pxmitframe->agg_num );*/
			SET_TX_DESC_DMA_TXAGG_NUM_8821C(pbuf, pxmitframe->agg_num);
		}
#endif /*CONFIG_USB_TX_AGGREGATION*/

		rtl8821c_fill_txdesc_vcs(adapter, pattrib, pbuf);

		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->ether_type != 0x88B4) &&
		    (pattrib->dhcp_pkt != 1) &&
		    (!bmcst)
#ifdef CONFIG_AUTO_AP_MODE
		    && (pattrib->pctrl != _TRUE)
#endif
		   ) {
			/* Non EAP & ARP & DHCP type data packet */

			if (pattrib->ampdu_en == _TRUE) {
				SET_TX_DESC_AGG_EN_8821C(pbuf, 1);
				SET_TX_DESC_MAX_AGG_NUM_8821C(pbuf, 0x1F);
				SET_TX_DESC_AMPDU_DENSITY_8821C(pbuf, pattrib->ampdu_spacing);
			} else
				SET_TX_DESC_BK_8821C(pbuf, 1);

			if (adapter->fix_bw != 0xFF)
				pattrib->bwmode =  adapter->fix_bw;

			rtl8821c_fill_txdesc_phy(adapter, pattrib, pbuf);

			if (phal_data->current_band_type == BAND_ON_5G)/*Data Rate Fallback Limit rate*/
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8821C(pbuf, 4);
			else
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8821C(pbuf, 0);

			if (phal_data->fw_ractrl == _FALSE) {
				SET_TX_DESC_USE_RATE_8821C(pbuf, 1);
				drv_fixed_reate = _TRUE;

				if (phal_data->INIDATA_RATE[pattrib->mac_id] & BIT(7))
					SET_TX_DESC_DATA_SHORT_8821C(pbuf, 1);

				SET_TX_DESC_DATARATE_8821C(pbuf, phal_data->INIDATA_RATE[pattrib->mac_id] & 0x7F);
			}

			/* modify data rate by iwpriv*/
			if (adapter->fix_rate != 0xFF) {
				SET_TX_DESC_USE_RATE_8821C(pbuf, 1);
				drv_fixed_reate = _TRUE;
				if (adapter->fix_rate & BIT(7))
					SET_TX_DESC_DATA_SHORT_8821C(pbuf, 1);
				SET_TX_DESC_DATARATE_8821C(pbuf, adapter->fix_rate & 0x7F);
				if (!adapter->data_fb)
					SET_TX_DESC_DISDATAFB_8821C(pbuf, 1);
			}

			if (pattrib->ldpc)
				SET_TX_DESC_DATA_LDPC_8821C(pbuf, 1);
			if (pattrib->stbc)
				SET_TX_DESC_DATA_STBC_8821C(pbuf, 1);

#ifdef CONFIG_CMCC_TEST
			SET_TX_DESC_DATA_SHORT_8821C(pbuf, 1); /* use cck short premble */
#endif
		} else {

			/*
			 * EAP data packet and ARP packet.
			 * Use the 1M data rate to send the EAP/ARP packet.
			 * This will maybe make the handshake smooth.
			 */
			SET_TX_DESC_USE_RATE_8821C(pbuf, 1);
			drv_fixed_reate = _TRUE;
			SET_TX_DESC_BK_8821C(pbuf, 1);

			/* HW will ignore this setting if the transmission rate is legacy OFDM.*/
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				SET_TX_DESC_DATA_SHORT_8821C(pbuf, 1);

			SET_TX_DESC_DATARATE_8821C(pbuf, MRateToHwRate(pmlmeext->tx_rate));

			if (bmcst)
				SET_TX_DESC_DISDATAFB_8821C(pbuf, 1);

			/*RTW_INFO(FUNC_ADPT_FMT ": SP Packet(0x%04X) rate=0x%x\n",
				FUNC_ADPT_ARG(adapter), pattrib->ether_type, MRateToHwRate(pmlmeext->tx_rate));*/
		}
#ifdef CONFIG_TDLS
#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __func__);
#endif
			SET_TX_DESC_SPE_RPT_8821C(pbuf, 1);
		}
#endif /* CONFIG_XMIT_ACK */
#endif

	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {

		SET_TX_DESC_USE_RATE_8821C(pbuf, 1);
		drv_fixed_reate = _TRUE;

		SET_TX_DESC_DATARATE_8821C(pbuf, MRateToHwRate(pattrib->rate));

		/* VHT NDPA or HT NDPA Packet for Beamformer.*/
#ifdef CONFIG_BEAMFORMING
		if ((pattrib->subtype == WIFI_NDPA) ||
		    ((pattrib->subtype == WIFI_ACTION_NOACK) && (pattrib->order == 1))) {
			SET_TX_DESC_NAVUSEHDR_8821C(pbuf, 1);

			SET_TX_DESC_DATA_BW_8821C(pbuf, rtl8821c_bw_mapping(adapter, pattrib));
			SET_TX_DESC_DATA_SC_8821C(pbuf, rtl8821c_sc_mapping(adapter, pattrib));
			/*SET_TX_DESC_RTS_SC_8821C(pbuf, rtl8821c_sc_mapping(adapter,pattrib)); ??????*/

			SET_TX_DESC_RTY_LMT_EN_8821C(pbuf, 1);
			SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(pbuf, 5);
			SET_TX_DESC_DISDATAFB_8821C(pbuf, 1);

			/*if(pattrib->rts_cca)
				SET_TX_DESC_NDPA_8821C(ptxdesc, 2);
			else*/
			SET_TX_DESC_NDPA_8821C(pbuf, 1);
		} else
#endif
		{
			SET_TX_DESC_RTY_LMT_EN_8821C(pbuf, 1);
			if (pattrib->retry_ctrl == _TRUE)
				SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(pbuf, 6);
			else
				SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(pbuf, 12);
		}

#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __func__);
#endif
			SET_TX_DESC_SPE_RPT_8821C(pbuf, 1);
		}
#endif /* CONFIG_XMIT_ACK */
	} else if (pxmitframe->frame_tag == TXAGG_FRAMETAG)
		RTW_INFO("%s: TXAGG_FRAMETAG\n", __func__);
#ifdef CONFIG_MP_INCLUDED
	else if (pxmitframe->frame_tag == MP_FRAMETAG) {
		RTW_INFO("%s: MP_FRAMETAG\n", __func__);
		fill_txdesc_for_mp(adapter, pbuf);
	}
#endif
	else {
		RTW_INFO("%s: frame_tag=0x%x\n", __func__, pxmitframe->frame_tag);

		SET_TX_DESC_USE_RATE_8821C(pbuf, 1);
		drv_fixed_reate = _TRUE;
		SET_TX_DESC_DATARATE_8821C(pbuf, MRateToHwRate(pmlmeext->tx_rate));
	}

	if (drv_fixed_reate == _TRUE)
		SET_TX_DESC_SW_DEFINE_8821C(pbuf, RA_SW_DEFINE_CONT);

	if (adapter->power_offset != 0)
		SET_TX_DESC_TXPWR_OFSET_8821C(pbuf, adapter->power_offset);


#ifdef CONFIG_BEAMFORMING
	SET_TX_DESC_G_ID_8821C(pbuf, pattrib->txbf_g_id);
	SET_TX_DESC_P_AID_8821C(pbuf, pattrib->txbf_p_aid);
#endif
	SET_TX_DESC_PORT_ID_8821C(pbuf, get_hw_port(adapter));

}

void rtl8821c_dbg_dump_tx_desc(PADAPTER adapter, int frame_tag, u8 *ptxdesc)
{
	u8 bDumpTxPkt;
	u8 bDumpTxDesc = _FALSE;


	rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_TXPKT, &bDumpTxPkt);

	/* 1 for data frame, 2 for mgnt frame */
	if (bDumpTxPkt == 1) {
		RTW_INFO("dump tx_desc for data frame\n");
		if ((frame_tag & 0x0f) == DATA_FRAMETAG)
			bDumpTxDesc = _TRUE;
	} else if (bDumpTxPkt == 2) {
		RTW_INFO("dump tx_desc for mgnt frame\n");
		if ((frame_tag & 0x0f) == MGNT_FRAMETAG)
			bDumpTxDesc = _TRUE;
	}

	/* 8821C TX SIZE = 48(HALMAC_TX_DESC_SIZE_8821C) */
	if (_TRUE == bDumpTxDesc) {
		RTW_INFO("=====================================\n");
		RTW_INFO("Offset00(0x%08x)\n", *((u32 *)(ptxdesc)));
		RTW_INFO("Offset04(0x%08x)\n", *((u32 *)(ptxdesc + 4)));
		RTW_INFO("Offset08(0x%08x)\n", *((u32 *)(ptxdesc + 8)));
		RTW_INFO("Offset12(0x%08x)\n", *((u32 *)(ptxdesc + 12)));
		RTW_INFO("Offset16(0x%08x)\n", *((u32 *)(ptxdesc + 16)));
		RTW_INFO("Offset20(0x%08x)\n", *((u32 *)(ptxdesc + 20)));
		RTW_INFO("Offset24(0x%08x)\n", *((u32 *)(ptxdesc + 24)));
		RTW_INFO("Offset28(0x%08x)\n", *((u32 *)(ptxdesc + 28)));
		RTW_INFO("Offset32(0x%08x)\n", *((u32 *)(ptxdesc + 32)));
		RTW_INFO("Offset36(0x%08x)\n", *((u32 *)(ptxdesc + 36)));
		RTW_INFO("Offset40(0x%08x)\n", *((u32 *)(ptxdesc + 40)));
		RTW_INFO("Offset44(0x%08x)\n", *((u32 *)(ptxdesc + 44)));
		RTW_INFO("=====================================\n");
	}
}

/*
 * Description:
 *
 * Parameters:
 *	pxmitframe	xmitframe
 *	pbuf		where to fill tx desc
 */
void rtl8821c_update_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	fill_default_txdesc(pxmitframe, pbuf);

#ifdef CONFIG_ANTENNA_DIVERSITY
	odm_set_tx_ant_by_tx_info(&GET_HAL_DATA(pxmitframe->padapter)->odmpriv, pbuf, pxmitframe->attrib.mac_id);
#endif /* CONFIG_ANTENNA_DIVERSITY */

	rtl8821c_cal_txdesc_chksum(pxmitframe->padapter, pbuf);
	rtl8821c_dbg_dump_tx_desc(pxmitframe->padapter, pxmitframe->frame_tag, pbuf);
}

/*
 * Description:
 *	In normal chip, we should send some packet to HW which will be used by FW
 *	in FW LPS mode.
 *	The function is to fill the Tx descriptor of this packets,
 *	then FW can tell HW to send these packet directly.
 */
static void fill_fake_txdesc(PADAPTER adapter, u8 *pDesc, u32 BufferLen,
			     u8 IsPsPoll, u8 IsBTQosNull, u8 bDataFrame)
{
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;

	/* Clear all status */
	_rtw_memset(pDesc, 0, HALMAC_TX_DESC_SIZE_8821C);

	SET_TX_DESC_LS_8821C(pDesc, 1);

	SET_TX_DESC_OFFSET_8821C(pDesc, HALMAC_TX_DESC_SIZE_8821C);

	SET_TX_DESC_TXPKTSIZE_8821C(pDesc, BufferLen);
	SET_TX_DESC_QSEL_8821C(pDesc, QSLT_MGNT); /* Fixed queue of Mgnt queue */

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
		SET_TX_DESC_RATE_ID_8821C(pDesc, RATEID_IDX_B);
	else
		SET_TX_DESC_RATE_ID_8821C(pDesc, RATEID_IDX_G);

	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by HW */
	if (_TRUE == IsPsPoll)
		SET_TX_DESC_NAVUSEHDR_8821C(pDesc, 1);
	else {
		SET_TX_DESC_DISQSELSEQ_8821C(pDesc, 1);
		SET_TX_DESC_EN_HWSEQ_8821C(pDesc, 1);
		SET_TX_DESC_HW_SSN_SEL_8821C(pDesc, 0);/*pattrib->hw_ssn_sel*/
		SET_TX_DESC_EN_HWEXSEQ_8821C(pDesc, 0);
	}

	if (_TRUE == IsBTQosNull)
		SET_TX_DESC_BT_NULL_8821C(pDesc, 1);

	SET_TX_DESC_USE_RATE_8821C(pDesc, 1);
	SET_TX_DESC_DATARATE_8821C(pDesc, MRateToHwRate(pmlmeext->tx_rate));

	/*
	 * Encrypt the data frame if under security mode excepct null data.
	 */
	if (_TRUE == bDataFrame) {
		u32 EncAlg;

		EncAlg = adapter->securitypriv.dot11PrivacyAlgrthm;
		switch (EncAlg) {
		case _NO_PRIVACY_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x0);
			break;
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x1);
			break;
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x2);
			break;
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x3);
			break;
		default:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x0);
			break;
		}
	}
	SET_TX_DESC_PORT_ID_8821C(pDesc, get_hw_port(adapter));
	SET_TX_DESC_MULTIPLE_PORT_8821C(pDesc, get_hw_port(adapter));

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	/*
	 * USB interface drop packet if the checksum of descriptor isn't correct.
	 * Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	 */
	rtl8821c_cal_txdesc_chksum(adapter, pDesc);
#endif
}

void rtl8821c_rxdesc2attribute(struct rx_pkt_attrib *pattrib, u8 *desc)
{
	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->pkt_len = (u16)GET_RX_DESC_PKT_LEN_8821C(desc);
	pattrib->crc_err = (u8)GET_RX_DESC_CRC32_8821C(desc);
	pattrib->icv_err = (u8)GET_RX_DESC_ICV_ERR_8821C(desc);

	pattrib->drvinfo_sz = (u8)GET_RX_DESC_DRV_INFO_SIZE_8821C(desc) << 3;
	pattrib->encrypt = (u8)GET_RX_DESC_SECURITY_8821C(desc);
	pattrib->qos = (u8)GET_RX_DESC_QOS_8821C(desc);
	pattrib->shift_sz = (u8)GET_RX_DESC_SHIFT_8821C(desc);
	pattrib->physt = (u8)GET_RX_DESC_PHYST_8821C(desc);
	pattrib->bdecrypted = (u8)GET_RX_DESC_SWDEC_8821C(desc) ? 0 : 1;

	pattrib->priority = (u8)GET_RX_DESC_TID_8821C(desc);
	pattrib->amsdu = (u8)GET_RX_DESC_AMSDU_8821C(desc);
	pattrib->mdata = (u8)GET_RX_DESC_MD_8821C(desc);
	pattrib->mfrag = (u8)GET_RX_DESC_MF_8821C(desc);

	pattrib->seq_num = (u16)GET_RX_DESC_SEQ_8821C(desc);
	pattrib->frag_num = (u8)GET_RX_DESC_FRAG_8821C(desc);
	pattrib->ppdu_cnt = (u8)GET_RX_DESC_PPDU_CNT_8821C(desc);

	if (GET_RX_DESC_C2H_8821C(desc))
		pattrib->pkt_rpt_type = C2H_PACKET;
	else
		pattrib->pkt_rpt_type = NORMAL_RX;


	pattrib->data_rate = (u8)GET_RX_DESC_RX_RATE_8821C(desc);

}

void rtl8821c_query_rx_desc(union recv_frame *precvframe, u8 *pdesc)
{
	rtl8821c_rxdesc2attribute(&precvframe->u.hdr.attrib, pdesc);
}

void rtl8821c_set_hal_ops(PADAPTER adapter)
{
	struct hal_ops *ops_func = &adapter->hal_func;

	/*** initialize section ***/
	ops_func->read_chip_version = read_chip_version;
	/*
		ops->init_default_value = NULL;
	*/
	ops_func->read_adapter_info = rtl8821c_read_efuse;
	ops_func->hal_power_on = rtl8821c_power_on;
	ops_func->hal_power_off = rtl8821c_power_off;

	ops_func->dm_init = rtl8821c_phy_init_dm_priv;
	ops_func->dm_deinit = rtl8821c_phy_deinit_dm_priv;

	/*** xmit section ***/
	/*
		ops->init_xmit_priv = NULL;
		ops->free_xmit_priv = NULL;
		ops->hal_xmit = NULL;
		ops->mgnt_xmit = NULL;
		ops->hal_xmitframe_enqueue = NULL;
	#ifdef CONFIG_XMIT_THREAD_MODE
		ops->xmit_thread_handler = NULL;
	#endif
	*/
	/*
		ops_func->run_thread = rtl8821c_run_thread;
		ops_func->cancel_thread = rtl8821c_cancel_thread;
	*/
	/*** recv section ***/
	/*
		ops->init_recv_priv = NULL;
		ops->free_recv_priv = NULL;
	#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
		ops->inirp_init = NULL;
		ops->inirp_deinit = NULL;
	#endif
	*/
	/*** interrupt hdl section ***/
	/*
		ops->enable_interrupt = NULL;
		ops->disable_interrupt = NULL;
	*/
	ops_func->check_ips_status = check_ips_status;
	/*
	#if defined(CONFIG_PCI_HCI)
		ops->interrupt_handler = NULL;
	#endif
	#if defined(CONFIG_USB_HCI) && defined(CONFIG_SUPPORT_USB_INT)
		ops->interrupt_handler = NULL;
	#endif
	#if defined(CONFIG_PCI_HCI)
		ops->irp_reset = NULL;
	#endif
	*/

	/*** DM section ***/
	/*
		ops->InitSwLeds = NULL;
		ops->DeInitSwLeds = NULL;
	*/
	ops_func->set_chnl_bw_handler = rtl8821c_set_channel_bw;

	ops_func->set_tx_power_level_handler = rtl8821c_set_tx_power_level;
	ops_func->get_tx_power_level_handler = rtl8821c_get_tx_power_level;

	ops_func->set_tx_power_index_handler = rtl8821c_set_tx_power_index;
	ops_func->get_tx_power_index_handler = rtl8821c_get_tx_power_index;

	ops_func->hal_dm_watchdog = rtl8821c_phy_haldm_watchdog;
#ifdef CONFIG_LPS_LCLK_WD_TIMER
	ops_func->hal_dm_watchdog_in_lps = rtl8821c_phy_haldm_watchdog_in_lps;
#endif

	ops_func->GetHalODMVarHandler = GetHalODMVar;
	ops_func->SetHalODMVarHandler = SetHalODMVar;

	ops_func->update_ra_mask_handler = update_ra_mask_8821c;
	ops_func->SetBeaconRelatedRegistersHandler = set_beacon_related_registers;

#ifdef CONFIG_ANTENNA_DIVERSITY
	/*
		ops->AntDivBeforeLinkHandler = NULL;
		ops->AntDivCompareHandler = NULL;
	*/
#endif
	/*
		ops->interface_ps_func = NULL;
	*/

	ops_func->read_bbreg = rtl8821c_read_bb_reg;
	ops_func->write_bbreg = rtl8821c_write_bb_reg;
	ops_func->read_rfreg = rtl8821c_read_rf_reg;
	ops_func->write_rfreg = rtl8821c_write_rf_reg;

#ifdef CONFIG_HOSTAPD_MLME
	/*
		ops->hostap_mgnt_xmit_entry = NULL;
	*/
#endif
	/*
		ops->EfusePowerSwitch = NULL;
		ops->BTEfusePowerSwitch = NULL;
		ops->ReadEFuse = NULL;
		ops->EFUSEGetEfuseDefinition = NULL;
		ops->EfuseGetCurrentSize = NULL;
		ops->Efuse_PgPacketRead = NULL;
		ops->Efuse_PgPacketWrite = NULL;
		ops->Efuse_WordEnableDataWrite = NULL;
		ops->Efuse_PgPacketWrite_BT = NULL;
	*/
#ifdef DBG_CONFIG_ERROR_DETECT
	ops_func->sreset_init_value = sreset_init_value;
	ops_func->sreset_reset_value = sreset_reset_value;
	ops_func->silentreset = sreset_reset;
	ops_func->sreset_xmit_status_check = xmit_status_check;
	ops_func->sreset_linked_status_check  = linked_status_check;
	ops_func->sreset_get_wifi_status  = sreset_get_wifi_status;
	ops_func->sreset_inprogress = sreset_inprogress;
#endif /* DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_IOL
	/*
		ops->IOL_exec_cmds_sync = NULL;
	*/
#endif

	ops_func->hal_notch_filter = rtl8821c_notch_filter_switch;
	ops_func->hal_mac_c2h_handler = c2h_handler_rtl8821c;
	ops_func->fill_h2c_cmd = rtl8821c_fillh2ccmd;
	ops_func->fill_fake_txdesc = fill_fake_txdesc;
	ops_func->fw_dl = rtl8821c_fw_dl;

#ifdef CONFIG_LPS_PG
	ops_func->fw_mem_dl = rtl8821c_fw_mem_dl;
#endif
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	/*
		ops->clear_interrupt = NULL;
	*/
#endif

	ops_func->hal_get_tx_buff_rsvd_page_num = get_txbuffer_rsvdpagenum;

#ifdef CONFIG_GPIO_API
	/*
		ops->update_hisr_hsisr_ind = NULL;
	*/
#endif

	ops_func->fw_correct_bcn = rtl8821c_fw_update_beacon_cmd;

	/* HALMAC related functions */
	ops_func->init_mac_register = rtl8821c_init_phy_parameter_mac;
	ops_func->init_phy = rtl8821c_phy_init;

}
