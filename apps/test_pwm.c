

/*================================================================
 *
 *
 *   文件名称：test_pwm.c
 *   创 建 者：肖飞
 *   创建日期：2019年10月11日 星期五 16时26分47秒
 *   修改日期：2019年10月11日 星期五 17时42分19秒
 *   描    述：
 *
 *================================================================*/
#include "stm32f2xx_hal.h"
#include "cmsis_os.h"
#include "stm32f2xx_hal_tim.h"
#include "test_pwm.h"

extern TIM_HandleTypeDef htim4;

uint16_t dutyCycle = 0;

void test_pwm(void const *argument)
{
	HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);

	while(1) {

		while (dutyCycle < 1000) {
			dutyCycle++;
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, dutyCycle);
			osDelay(1);
		}

		while (dutyCycle) {
			dutyCycle--;
			__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, dutyCycle);
			osDelay(1);
		}

		osDelay(200);
	}
}
