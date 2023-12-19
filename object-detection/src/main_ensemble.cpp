/*
 * SPDX-FileCopyrightText: Copyright 2021-2023 Arm Limited and/or its
 * affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * This object detection example is intended to work with the
 * CMSIS pack produced by ml-embedded-eval-kit. The pack consists
 * of platform agnostic end-to-end ML use case API's that can be
 * used to construct ML examples for any target that can support
 * the memory requirements for TensorFlow-Lite-Micro framework and
 * some heap for the API runtime.
 */
#include "BufAttributes.hpp" /* Buffer attributes to be applied */
#include "Classifier.hpp"    /* Classifier for the result */
#include "DetectionResult.hpp"
#include "DetectorPostProcessing.hpp" /* Post Process */
#include "DetectorPreProcessing.hpp"  /* Pre Process */
#include "InputFiles.hpp"             /* Baked-in input (not needed for live data) */
#include "YoloFastestModel.hpp"       /* Model API */

/* Platform dependent files */
#include "RTE_Components.h"  /* Provides definition for CMSIS_device_header */
#include CMSIS_device_header /* Gives us IRQ num, base addresses. */
#include "platform_drivers.h"      /* Board initialisation */
#include "log_macros.h"      /* Logging macros (optional) */
#include "Profiler.hpp"
//LCD
#include "ScreenLayout.hpp"
#include "lvgl.h"
#include "lv_port.h"
#include "lv_paint_utils.h"

//camera
#include "image_data.h"

// lv_style_t boxStyle;
// static lv_color_t  lvgl_image[LIMAGE_Y][LIMAGE_X] __attribute__((section(".bss.lcd_image_buf")));
namespace {
lv_style_t boxStyle;
lv_color_t  lvgl_image[LIMAGE_Y][LIMAGE_X] __attribute__((section(".bss.lcd_image_buf")));                      // 192x192x2 = 73,728
};
namespace arm {
namespace app {
    /* Tensor arena buffer */
    static uint8_t tensorArena[ACTIVATION_BUF_SZ] ACTIVATION_BUF_ATTRIBUTE;

    /* Optional getter function for the model pointer and its size. */
    namespace object_detection {
        extern uint8_t* GetModelPointer();
        extern size_t GetModelLen();
    } /* namespace object_detection */
} /* namespace app */
} /* namespace arm */

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
__asm("  .global __ARM_use_no_argv\n");
#endif

static void DrawDetectionBoxes(const std::vector<arm::app::object_detection::DetectionResult>& results,
                                   int imgInputCols, int imgInputRows)
    {
        lv_obj_t *frame = arm::app::ScreenLayoutImageHolderObject();
        float xScale = (float) lv_obj_get_content_width(frame) / imgInputCols;
        float yScale = (float) lv_obj_get_content_height(frame) / imgInputRows;

        //Delete Boxes
        // Assume that child 0 of the frame is the image itself
        int children = lv_obj_get_child_cnt(frame);
        while (children > 1) {
            lv_obj_del(lv_obj_get_child(frame, 1));
            children--;
        }
        //Create Box
        for (const auto& result: results) {
            int x0 = floor(result.m_x0 * xScale);
            int y0 = floor(result.m_y0 * yScale);
            int w = ceil(result.m_w * xScale);
            int h = ceil(result.m_h * yScale);
            lv_obj_t *box = lv_obj_create(frame);
            lv_obj_set_size(box, w, h);
            lv_obj_add_style(box, &boxStyle, LV_PART_MAIN);
            lv_obj_set_pos(box, x0, y0);
        }
    }

bool LCDInit()
    {
        arm::app::ScreenLayoutInit(lvgl_image, sizeof lvgl_image, LIMAGE_X, LIMAGE_Y, LV_ZOOM);
        uint32_t lv_lock_state = lv_port_lock();
        
        lv_label_set_text_static(arm::app::ScreenLayoutHeaderObject(), "Face Detection");
        lv_label_set_text_static(arm::app::ScreenLayoutLabelObject(0), "Faces Detected: 0");
        lv_label_set_text_static(arm::app::ScreenLayoutLabelObject(1), "192px image (24-bit)");

        lv_style_init(&boxStyle);
        lv_style_set_bg_opa(&boxStyle, LV_OPA_TRANSP);
        lv_style_set_pad_all(&boxStyle, 0);
        lv_style_set_border_width(&boxStyle, 0);
        lv_style_set_outline_width(&boxStyle, 2);
        lv_style_set_outline_pad(&boxStyle, 0);
        lv_style_set_outline_color(&boxStyle, lv_theme_get_color_primary(arm::app::ScreenLayoutHeaderObject()));
        lv_style_set_radius(&boxStyle, 4);
        lv_port_unlock(lv_lock_state);

        /* Initialise the camera */
        int err = image_init();
        if (0 != err) {
            printf_err("hal_image_init failed with error: %d\n", err);
        }

        return true;
    }

int main(void)
{
    /* Initialise the UART module to allow printf related functions (if using retarget) */
    int ret = 0;
    ret = platform_init();
    if (ret!=0){
        printf_err("init failed \n");
        return 1;
    }
    if(!LCDInit()){
        printf_err("LCD init failed \n");
        return 1;
    }
    BOARD_LED2_Control(BOARD_LED_STATE_HIGH); //green led on
    arm::app::Profiler profiler{"object_detection"};
    info("starting the model load \n");

    /* Model object creation and initialisation. */
    arm::app::YoloFastestModel model;
    if (!model.Init(arm::app::tensorArena,
                    sizeof(arm::app::tensorArena),
                    arm::app::object_detection::GetModelPointer(),
                    arm::app::object_detection::GetModelLen())) {
        printf_err("Failed to initialise model\n");
        BOARD_LED1_Control(BOARD_LED_STATE_HIGH); //red led on
        return 1;
    }
    do{
        TfLiteTensor* inputTensor   = model.GetInputTensor(0);
        TfLiteTensor* outputTensor0 = model.GetOutputTensor(0);
        TfLiteTensor* outputTensor1 = model.GetOutputTensor(1);

        if (!inputTensor->dims) {
            printf_err("Invalid input tensor dims\n");
            BOARD_LED1_Control(BOARD_LED_STATE_HIGH);
            return 1;
        } else if (inputTensor->dims->size < 3) {
            printf_err("Input tensor dimension should be >= 3\n");
            BOARD_LED1_Control(BOARD_LED_STATE_HIGH);
            return 1;
        }

        TfLiteIntArray* inputShape = model.GetInputShape(0);

        const int inputImgCols = inputShape->data[arm::app::YoloFastestModel::ms_inputColsIdx];
        const int inputImgRows = inputShape->data[arm::app::YoloFastestModel::ms_inputRowsIdx];

        /* Set up pre and post-processing. */
        arm::app::DetectorPreProcess preProcess =
            arm::app::DetectorPreProcess(inputTensor, true, model.IsDataSigned());

        std::vector<arm::app::object_detection::DetectionResult> results;
        const arm::app::object_detection::PostProcessParams postProcessParams{
            inputImgRows,
            inputImgCols,
            arm::app::object_detection::originalImageSize,
            arm::app::object_detection::anchor1,
            arm::app::object_detection::anchor2};
        arm::app::DetectorPostProcess postProcess =
            arm::app::DetectorPostProcess(outputTensor0, outputTensor1, results, postProcessParams);

        results.clear();

        // const uint8_t* currImage = get_img_array(0);
        const uint8_t* currImage = get_image_data(inputImgCols, inputImgRows);
        if (!currImage) {
            printf_err("hal_get_image_data failed");
            return 1;
        }

        {
            ScopedLVGLLock lv_lock;

                /* Display this image on the LCD. */
            write_to_lvgl_buf(inputImgCols, inputImgRows,
                            currImage, &lvgl_image[0][0]);
            lv_obj_invalidate(arm::app::ScreenLayoutImageObject());

            //lv_led_on(arm::app::ScreenLayoutLEDObject());

            const size_t copySz = inputTensor->bytes;

            /* Run the pre-processing, inference and post-processing. */
            if (!preProcess.DoPreProcess(currImage, copySz)) {
                printf_err("Pre-processing failed.");
                return 3;
            }

            /* Run inference over this image. */
            profiler.StartProfiling("Inference");
            if (!model.RunInference()) {
                printf_err("Inference failed.");
                return 3;
            }
            profiler.StopProfiling();
            if (!postProcess.DoPostProcess()) {
                printf_err("Post-processing failed.");
                return 3;
            }

            lv_label_set_text_fmt(arm::app::ScreenLayoutLabelObject(0), "Faces Detected: %i", results.size());

            /* Draw boxes. */
            DrawDetectionBoxes(results, inputImgCols, inputImgRows);

        } // ScopedLVGLLock

        /* Log the results. */
        for (uint32_t i = 0; i < results.size(); ++i) {
            info("Detection at index %" PRIu32 ", at x-coordinate %" PRIu32 ", y-coordinate %" PRIu32
                ", width %" PRIu32 ", height %" PRIu32 "\n",
                i,
                results[i].m_x0,
                results[i].m_y0,
                results[i].m_w,
                results[i].m_h);
        }
        profiler.PrintProfilingResult();

    }while(1);

    return 0;
}
