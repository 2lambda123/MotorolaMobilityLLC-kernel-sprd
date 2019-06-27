export BSP_KERNEL_ARCH="arm64"
export BSP_KERNEL_DIFF_CONFIG_ARCH="sprd-diffconfig/sharkl3/$BSP_KERNEL_ARCH"
export BSP_KERNEL_DIFF_CONFIG_COMMON="sprd-diffconfig/sharkl3/common"
if [ -z $BSP_KERNEL_PATH ]; then
	BSP_KERNEL_PATH="."
fi
export BSP_KERNEL_CROSS_COMPILE="$BSP_KERNEL_PATH/../../toolchain/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-"
export BSP_BOARD_NAME="s9863a3c10"

export BSP_BOARD_WCN_CONFIG=""
export BSP_BOARD_EXT_PMIC_CONFIG=""
export BSP_PRODUCT_GO_DEVICE=""
export BSP_BOARD_FEATUREPHONE_CONFIG=""
export BSP_BOARD_TEE_CONFIG=""

if [ "$BSP_BOARD_FEATUREPHONE_CONFIG" == "true" ]; then
	export BSP_BOARD_TEE_64BIT="false"
else
	export BSP_BOARD_TEE_64BIT="true"
fi

