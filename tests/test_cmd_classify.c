/* tests/test_cmd_classify.c */
#include "test_framework.h"
#include "cmd_classify.h"
#include <string.h>

/* --- NULL and empty input --- */
int test_cmd_classify_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify(NULL, CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_empty(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- Linux safe commands --- */
int test_cmd_classify_linux_ls(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls -la /etc", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_linux_cat(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat /etc/hostname", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_linux_grep(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("grep -r 'foo' /var/log", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_linux_ping(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping -c 4 8.8.8.8", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- Linux write commands --- */
int test_cmd_classify_linux_mv(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mv foo.txt bar.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_cp(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cp a.conf a.bak", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_mkdir(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mkdir -p /tmp/build", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_chmod(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("chmod 755 script.sh", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_apt_install(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("apt install nginx", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_git_push(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("git push origin main", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_systemctl_start(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl start nginx", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_docker_run(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("docker run -d nginx", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* --- Linux critical commands --- */
int test_cmd_classify_linux_rm(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("rm -rf /tmp/build", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_reboot(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reboot", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_shutdown(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("shutdown -h now", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_kill9(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("kill -9 1234", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_killall(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("killall nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_dd(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("dd if=/dev/zero of=/dev/sda", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_mkfs(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mkfs.ext4 /dev/sdb1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_iptables_flush(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("iptables -F", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_systemctl_stop(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl stop nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_docker_rm_f(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("docker rm -f container1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_kubectl_delete(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("kubectl delete pod mypod", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}
