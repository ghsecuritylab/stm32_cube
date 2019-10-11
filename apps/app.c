

/*================================================================
 *
 *
 *   文件名称：app.c
 *   创 建 者：肖飞
 *   创建日期：2019年10月11日 星期五 16时54分03秒
 *   修改日期：2019年10月11日 星期五 16时56分12秒
 *   描    述：
 *
 *================================================================*/
#include "app.h"
#include "stm32f2xx_hal.h"
#include "cmsis_os.h"

#include "task_probe_tool.h"
#include "test_pwm.h"

void app(void const *argument)
{

	osThreadDef(probe_tool, task_probe_tool, osPriorityNormal, 0, 128 * 4);
	osThreadCreate(osThread(probe_tool), NULL);
	osThreadDef(test_pwm, test_pwm, osPriorityNormal, 0, 128);
	osThreadCreate(osThread(test_pwm), NULL);

	while(1) {
		osDelay(200);
	}
}
