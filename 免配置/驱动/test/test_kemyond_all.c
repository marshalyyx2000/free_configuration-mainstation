#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "kemyond.h"

static volatile sig_atomic_t g_stop = 0;

static void sig_handler(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void setup_signal(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);

    /*
     * 不设置 SA_RESTART
     * Ctrl+C 可以打断 fgets/sleep/usleep
     */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static int read_int(const char *prompt, int *value)
{
    char buf[64];

    if (!value)
        return -1;

    printf("%s", prompt);
    fflush(stdout);

    errno = 0;
    if (!fgets(buf, sizeof(buf), stdin)) {
        if (g_stop || errno == EINTR)
            return -2;
        return -1;
    }

    if (sscanf(buf, "%d", value) != 1)
        return -1;

    return 0;
}

static void interruptible_sleep_ms(int ms)
{
    int left_ms;
    int step_ms;

    left_ms = ms;

    while (left_ms > 0 && !g_stop) {
        step_ms = left_ms > 100 ? 100 : left_ms;
        usleep(step_ms * 1000);
        left_ms -= step_ms;
    }
}

static void print_led_id(void)
{
    printf("LED ID:\n");
    printf("  %d: LED_RUN\n", LED_RUN);
    printf("  %d: LED_ALARM\n", LED_ALARM);
    printf("  %d: LED_BACKUP\n", LED_BACKUP);
}

static void test_set_led(void)
{
    int id;
    int value;
    int ret;

    print_led_id();

    if (read_int("请输入LED ID: ", &id) < 0)
        return;

    if (read_int("请输入LED状态, 1=亮, 0=灭: ", &value) < 0)
        return;

    if (value != 0 && value != 1) {
        printf("value invalid, only 0 or 1\n");
        return;
    }

    ret = SetLEDValue(id, value ? true : false);
    if (ret < 0)
        printf("SetLEDValue failed\n");
    else
        printf("SetLEDValue ok, id=%d value=%d\n", id, value);
}

static void test_get_all_led(void)
{
    int ret;

    ret = GetLEDValue(LED_RUN);
    printf("LED_RUN    = %d, %s\n",
           ret,
           ret < 0 ? "读取失败" : (ret ? "亮" : "灭"));

    ret = GetLEDValue(LED_ALARM);
    printf("LED_ALARM  = %d, %s\n",
           ret,
           ret < 0 ? "读取失败" : (ret ? "亮" : "灭"));

    ret = GetLEDValue(LED_BACKUP);
    printf("LED_BACKUP = %d, %s\n",
           ret,
           ret < 0 ? "读取失败" : (ret ? "亮" : "灭"));
}

static void test_get_cpu_temp(void)
{
    int temp;

    temp = GetCpuTemp();
    if (temp < 0) {
        printf("GetCpuTemp failed\n");
        return;
    }

    printf("CPU temp raw = %d millidegree Celsius\n", temp);
    printf("CPU temp     = %.3f C\n", temp / 1000.0);
}

static void print_all_powerlost_once(void)
{
    int ret;

    ret = GetPowerLost(POWER_LOST_1);
    printf("POWER_LOST_1 = %d, %s\n",
           ret,
           ret < 0 ? "读取失败" : (ret ? "正常" : "失电"));

    ret = GetPowerLost(POWER_LOST_2);
    printf("POWER_LOST_2 = %d, %s\n",
           ret,
           ret < 0 ? "读取失败" : (ret ? "正常" : "失电"));
}

/*
 * 循环读取全部失电状态
 * Ctrl+C 停止循环，返回菜单
 */
static void test_get_all_powerlost_loop(void)
{
    g_stop = 0;
    setup_signal();

    printf("开始循环读取全部失电状态，Ctrl+C停止...\n");
    printf("说明：1=正常，0=失电\n");

    while (!g_stop) {
        print_all_powerlost_once();
        printf("-----------------------------\n");

        interruptible_sleep_ms(1000);
    }

    printf("\n停止失电状态循环读取\n");

    g_stop = 0;
    setup_signal();
}

/*
 * WatchDog 测试：
 *
 * 1. 设置超时时间 timeout_sec
 * 2. 设置超时时间内喂狗次数 feed_count
 *    feed_count = 0 表示不喂狗
 * 3. 设置是否等待重启
 *    0 = 不重启，喂完后关闭 watchdog
 *    1 = 喂完后停止喂狗，等待系统复位
 */
static void test_watchdog_menu(void)
{
    int timeout_sec;
    int real_timeout_sec;
    int feed_count;
    int reboot_enable;
    int interval_ms;
    int wait_left;
    int i;
    int ret;

    g_stop = 0;
    setup_signal();

    if (read_int("请输入watchdog超时时间，单位秒: ", &timeout_sec) < 0)
        return;

    if (timeout_sec <= 0) {
        printf("invalid timeout\n");
        return;
    }

    if (read_int("请输入超时时间内喂狗次数，0表示不喂狗: ", &feed_count) < 0)
        return;

    if (feed_count < 0) {
        printf("invalid feed_count\n");
        return;
    }

    if (read_int("是否测试超时自动重启？0=否，1=是: ", &reboot_enable) < 0)
        return;

    if (reboot_enable != 0 && reboot_enable != 1) {
        printf("invalid reboot_enable\n");
        return;
    }

    printf("打开watchdog...\n");

    ret = OpenWatchDog();
    if (ret < 0) {
        printf("OpenWatchDog failed\n");
        return;
    }

    ret = SetWatchDog(timeout_sec);
    if (ret < 0) {
        printf("SetWatchDog failed\n");
        CloseWatchDog();
        return;
    }

    real_timeout_sec = timeout_sec;

    ret = GetWatchDog();
    if (ret >= 0) {
        real_timeout_sec = ret;
        printf("WatchDog实际超时时间 = %d 秒\n", real_timeout_sec);
    } else {
        printf("GetWatchDog failed, 使用输入超时时间 = %d 秒\n", real_timeout_sec);
    }

    if (feed_count > 0)
        interval_ms = (real_timeout_sec * 1000) / feed_count;
    else
        interval_ms = 0;

    if (feed_count > 0 && interval_ms <= 0)
        interval_ms = 1;

    printf("====================================\n");
    printf("timeout_sec      = %d\n", timeout_sec);
    printf("real_timeout_sec = %d\n", real_timeout_sec);
    printf("feed_count       = %d\n", feed_count);
    printf("interval_ms      = %d\n", interval_ms);
    printf("reboot_enable    = %d\n", reboot_enable);
    printf("====================================\n");

    if (feed_count > 0) {
        printf("开始喂狗，Ctrl+C可中断...\n");

        for (i = 0; i < feed_count && !g_stop; i++) {
            ret = FeedWatchDog();
            if (ret < 0) {
                printf("FeedWatchDog failed, index=%d\n", i + 1);
                CloseWatchDog();
                return;
            }

            printf("FeedWatchDog ok, %d/%d\n", i + 1, feed_count);

            if (i != feed_count - 1)
                interruptible_sleep_ms(interval_ms);
        }

        printf("喂狗完成，共喂狗 %d 次\n", i);
    } else {
        printf("feed_count=0，不喂狗\n");
    }

    if (g_stop) {
        printf("\n收到退出信号，关闭watchdog\n");
        CloseWatchDog();

        g_stop = 0;
        setup_signal();
        return;
    }

    if (reboot_enable) {
        printf("====================================\n");
        printf("喂狗完成，已停止喂狗\n");
        printf("开始等待watchdog超时复位\n");
        printf("等待超时时间约为 %d 秒\n", real_timeout_sec);
        printf("如果watchdog有效，系统应在倒计时结束后复位\n");
        printf("Ctrl+C 可取消测试并关闭watchdog\n");
        printf("====================================\n");

        wait_left = real_timeout_sec;

        while (wait_left > 0 && !g_stop) {
            printf("等待复位中，剩余约 %d 秒\n", wait_left);
            sleep(1);
            wait_left--;
        }

        if (!g_stop) {
            printf("倒计时结束，继续等待系统复位...\n");

            while (!g_stop)
                sleep(1);
        }

        printf("\n收到退出信号，取消重启测试，关闭watchdog\n");

        CloseWatchDog();

        g_stop = 0;
        setup_signal();
        return;
    }

    printf("不测试重启，关闭watchdog\n");

    ret = CloseWatchDog();
    if (ret < 0)
        printf("CloseWatchDog failed\n");
    else
        printf("CloseWatchDog ok\n");

    g_stop = 0;
    setup_signal();
}

static void show_menu(void)
{
    printf("\n");
    printf("========== KEMYOND TEST ==========\n");
    printf("1. 设置单个LED\n");
    printf("2. 读取全部LED\n");
    printf("3. 读取CPU温度\n");
    printf("4. 循环读取全部失电状态\n");
    printf("5. WatchDog测试\n");
    printf("0. 退出\n");
}

int main(void)
{
    int cmd;
    int ret;

    setup_signal();

    while (!g_stop) {
        show_menu();

        ret = read_int("请选择: ", &cmd);
        if (ret < 0) {
            if (g_stop)
                break;

            printf("input error\n");
            continue;
        }

        switch (cmd) {
        case 1:
            test_set_led();
            break;

        case 2:
            test_get_all_led();
            break;

        case 3:
            test_get_cpu_temp();
            break;

        case 4:
            test_get_all_powerlost_loop();
            break;

        case 5:
            test_watchdog_menu();
            break;

        case 0:
            g_stop = 1;
            break;

        default:
            printf("invalid cmd\n");
            break;
        }
    }

    printf("\nexit\n");
    return 0;
}