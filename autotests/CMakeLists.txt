# SPDX-FileCopyrightText: 2026 Meluxon
# SPDX-License-Identifier: GPL-2.0-or-later

ecm_add_test(ite8297test.cpp TEST_NAME ite8297test LINK_LIBRARIES glowzone_core Qt6::Test)
ecm_add_test(ecservicetest.cpp TEST_NAME ecservicetest LINK_LIBRARIES glowzone_ecservice Qt6::Test)
ecm_add_test(modeltest.cpp TEST_NAME modeltest LINK_LIBRARIES glowzone_core Qt6::Test)
target_compile_definitions(modeltest PRIVATE GLOWZONE_UDEV_RULES="${PROJECT_SOURCE_DIR}/data/70-glowzone.rules")
ecm_add_test(fanstatetest.cpp TEST_NAME fanstatetest LINK_LIBRARIES glowzone_core Qt6::Test)
ecm_add_test(profilestest.cpp TEST_NAME profilestest LINK_LIBRARIES glowzone_core Qt6::Test)
ecm_add_test(lightappliertest.cpp TEST_NAME lightappliertest LINK_LIBRARIES glowzone_core Qt6::Test)
