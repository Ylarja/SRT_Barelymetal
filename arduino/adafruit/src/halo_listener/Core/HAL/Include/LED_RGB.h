#ifndef HAL_LED_RGB_H
#define HAL_LED_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

void HAL_LED_RGB_Init();
void HAL_LED_RGB_Toggle_Red();
void HAL_LED_RGB_Toggle_Green();
void HAL_LED_RGB_Toggle_Blue();
void HAL_LED_RGB_Red_On();
void HAL_LED_RGB_Red_Off();
void HAL_LED_RGB_Green_On();
void HAL_LED_RGB_Green_Off();
void HAL_LED_RGB_Blue_On();
void HAL_LED_RGB_Blue_Off();
void HAL_LED_RGB_Off();
void HAL_LED_RGB_On();
bool HAL_LED_RGB_Is_Red_On();
bool HAL_LED_RGB_Is_Green_On();
bool HAL_LED_RGB_Is_Blue_On();


#ifdef __cplusplus
}
#endif

#endif // HAL_LED_RGB_H