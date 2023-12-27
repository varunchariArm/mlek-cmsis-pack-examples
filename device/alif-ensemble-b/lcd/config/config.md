# CMSIS packs and Configurations for Alif Ensemble E7B

## LVGL CMSIS pack installation and configurations
1. Navigate to the [LVGL CMSIS directory](../../dependencies/lvgl/env_support/cmsis-pack/)
2. Update 'lv_conf_cmsis.h' with the 'lv_conf.h' file in the current directory
3. Update the 'gen_pack.sh' script as per [this](https://github.com/lvgl/lvgl/tree/master/env_support/cmsis-pack#step-2-check-update-and-run-the-gen_packsh)
4. Execute the 'gen_pack.sh' script to install LVGL CMSIS pack

## Alif Ensemble CMSIS pack installtion and  configurations
1. Navigate to the [Alif CMSIS directory](../../dependencies/cmsis-ensemble/)
2. Update [RTE_Device.h](../../dependencies/cmsis-ensemble/Device/E7/AE722F80F55D5XX/RTE_Device.h) with
    * RTE_ILI9806E_PANEL_DSI_COLOR_MODE       5
    * RTE_ARX3A0_CAMERA_SENSOR_CSI_CFG_FPS    40
3. Install the CMSIS Pack by running `cpackget pack add AlifSemiconductor.Ensemble.pdsc`

## Arm 2D configurations
1. Navigate to the directory where ARM-2D CMSIS pack is installed
2. Replace `{ARM2D dir}\{version}\Library\Include\template\arm_2d_cfg.h` with 'arm_2d_cfg.h' in the current directory
3. Delete **RTE** directory in the device directory (alif-ensemble-b)
4. Clean build the project to get updated RTE configs for Arm-2D
