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

/* --- Whitespace handling --- */
int test_cmd_classify_leading_whitespace(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("   ls -la", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_trailing_whitespace(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("rm -rf /tmp   ", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Path prefixes --- */
int test_cmd_classify_path_prefix(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/usr/bin/rm -rf /tmp", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_path_prefix_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/usr/bin/cat /etc/hosts", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- sudo/su escalation --- */
int test_cmd_classify_sudo_escalation(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sudo cat /etc/shadow", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_sudo_rm(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sudo rm -rf /var/lib/thing", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Pipelines: highest risk wins --- */
int test_cmd_classify_pipe_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat /etc/hosts | grep localhost", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_pipe_to_xargs_rm(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("find /tmp -name '*.tmp' | xargs rm", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_pipe_to_sh(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("curl http://example.com/script | sh", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_pipe_to_sudo_tee(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat file | sudo tee /etc/config", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Semicolons and && --- */
int test_cmd_classify_semicolon_worst_wins(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls; rm -rf /", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_and_worst_wins(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls && rm -rf /tmp", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Redirect handling --- */
int test_cmd_classify_redirect_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("echo foo > /tmp/bar", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_redirect_append(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("echo foo >> /tmp/bar", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_redirect_dev_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls > /dev/null", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_redirect_stderr_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls 2>/dev/null", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_redirect_stderr_stdout(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls 2>&1", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

/* --- Subcommand sensitivity --- */
int test_cmd_classify_systemctl_status_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl status nginx", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_sed_plain_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sed 's/foo/bar/' file.txt", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_sed_i_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sed -i 's/foo/bar/' file.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_curl_plain_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("curl http://example.com", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_curl_o_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("curl -o file.tar.gz http://example.com/file", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* --- Database CLI --- */
int test_cmd_classify_mysql_select_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mysql -e \"SELECT * FROM users\"", CMD_PLATFORM_LINUX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_mysql_drop_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mysql -e \"DROP TABLE users\"", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- ip commands --- */
int test_cmd_classify_ip_route_del_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ip route del default", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ip_addr_add_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ip addr add 10.0.0.1/24 dev eth0", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* --- cmd_classify_ex with reason --- */
int test_cmd_classify_ex_reason(void) {
    TEST_BEGIN();
    char reason[128] = {0};
    CmdSafetyLevel level = cmd_classify_ex("rm -rf /tmp", CMD_PLATFORM_LINUX, reason, sizeof(reason));
    ASSERT_EQ((int)level, (int)CMD_CRITICAL);
    ASSERT_TRUE(strlen(reason) > 0);
    TEST_END();
}

int test_cmd_classify_ex_null_reason(void) {
    TEST_BEGIN();
    CmdSafetyLevel level = cmd_classify_ex("rm -rf /tmp", CMD_PLATFORM_LINUX, NULL, 0);
    ASSERT_EQ((int)level, (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Cisco IOS --- */
int test_cmd_classify_ios_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show ip route", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_ios_ping_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping 10.0.0.1", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_ios_enable_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("enable", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_ios_conf_t_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure terminal", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_ios_write_mem_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("write memory", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_ios_ip_address_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ip address 10.0.0.1 255.255.255.0", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_ios_reload_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_write_erase_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("write erase", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_no_router_bgp_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no router bgp 65000", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_clear_bgp_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear ip bgp *", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_shutdown_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_case_insensitive(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("SHOW ip route", CMD_PLATFORM_CISCO_IOS), (int)CMD_SAFE);
    TEST_END();
}

/* --- Cisco NX-OS (IOS rules plus NX-OS extras) --- */
int test_cmd_classify_nxos_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show vlan", CMD_PLATFORM_CISCO_NXOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_nxos_feature_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("feature nv overlay", CMD_PLATFORM_CISCO_NXOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_nxos_no_vpc_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no vpc domain 100", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_nxos_reload_module_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload module 1", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Cisco ASA --- */
int test_cmd_classify_asa_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_CISCO_ASA), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_asa_nat_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("nat (inside,outside) dynamic interface", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_asa_no_failover_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no failover", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_asa_clear_configure_all_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear configure all", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Aruba OS-CX --- */
int test_cmd_classify_aruba_cx_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_aruba_cx_conf_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure terminal", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_aruba_cx_erase_startup_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("erase startup-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_aruba_cx_no_vsx_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no vsx", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- ArubaOS (wireless) --- */
int test_cmd_classify_aruba_os_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show ap database", CMD_PLATFORM_ARUBA_OS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_aruba_os_wlan_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("wlan ssid-profile corp", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_aruba_os_factory_reset_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("factory-reset", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_aruba_os_ap_wipe_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ap wipe out all", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- PAN-OS --- */
int test_cmd_classify_panos_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show system info", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_ping_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping host 10.0.0.1", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_commit_validate_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit validate", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_test_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("test security-policy-match", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_configure_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_panos_set_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("set network interface ethernet1/1 ip 10.0.0.1/24", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_panos_commit_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_commit_force_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit force", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_commit_all_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit-all", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_delete_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("delete network interface ethernet1/1", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_request_restart_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request restart system", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_request_license_info_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request license info", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_request_license_deactivate_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request license deactivate", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_pipe_modifier(void) {
    TEST_BEGIN();
    /* Pipe modifiers (| match, | except) should not change safety level */
    ASSERT_EQ((int)cmd_classify("show running-config | match ssl", CMD_PLATFORM_PANOS), (int)CMD_SAFE);
    TEST_END();
}

int test_cmd_classify_panos_clear_session_all_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear session all", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}
