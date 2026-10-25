#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * ================== 只需要改这一个宏 ==================
 * 0 = 编码器手动计圈（电机驱动关闭，手转车轮）
 * 1 = 编码器空载测试（左右电机持续正转，直到复位/断电）
 * 2 = 作业二：电机 PWM/驱动波形测试（左右电机自动正反转）
 * 3 = 编码器调试：左右电机持续正转，每 100 ms 输出 A/B 电平和原始计数
 * 4 = 编码器闭环：电机正转 4 圈 → 停 0.5 s → 反转 4 圈 → 停止
 *
 * 修改后必须重新编译、重新烧录，板子上运行的永远是最后烧录的那份固件。
 * ======================================================
 */
#ifndef TEST_SELECT
#define TEST_SELECT                 4
#endif

#define APP_MODE_MOTOR_TEST         0
#define APP_MODE_ENCODER_TEST       1

#if (TEST_SELECT == 2)

#define APP_MODE                    APP_MODE_MOTOR_TEST
#define ENCODER_TEST_MODE           0

#elif (TEST_SELECT == 1)

#define APP_MODE                    APP_MODE_ENCODER_TEST
#define ENCODER_TEST_MODE           1

#elif (TEST_SELECT == 3)

#define APP_MODE                    APP_MODE_ENCODER_TEST
#define ENCODER_TEST_MODE           2

#elif (TEST_SELECT == 4)

#define APP_MODE                    APP_MODE_ENCODER_TEST
#define ENCODER_TEST_MODE           3

#else

#define APP_MODE                    APP_MODE_ENCODER_TEST
#define ENCODER_TEST_MODE           0

#endif

/*
 * 空载测试（TEST_SELECT=1）时使用哪个电机：
 * 1 = 只让左电机转
 * 2 = 只让右电机转
 * 3 = 左右电机都转（当前默认）
 */
#ifndef ENCODER_NOLOAD_MOTOR
#define ENCODER_NOLOAD_MOTOR        3
#endif

/*
 * 四圈往返任务（TEST_SELECT=4）使用哪个电机：
 * 1 = 只左电机
 * 2 = 只右电机
 * 3 = 左右电机一起（当前默认）
 */
#ifndef ENCODER_FOUR_TURN_MOTOR
#define ENCODER_FOUR_TURN_MOTOR     3
#endif

#endif
