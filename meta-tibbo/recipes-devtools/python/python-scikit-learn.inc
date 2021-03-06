SUMMARY = "Machine Learning in Python"
HOMEPAGE = "https://scikit-learn.org/stable/"
SECTION = "devel/python"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://PKG-INFO;md5=4a2bb4f4ec2b68ec46d5d1be3371b2c7"

inherit pypi setuptools3

RDEPENDS_${PN} += "${PYTHON_PN}-numpy ${PYTHON_PN}-scipy ${PYTHON_PN}-joblib"

BBCLASSEXTEND = "native nativesdk"
