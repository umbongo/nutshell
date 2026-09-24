/* tests/test_cmd_classify.c */
#include "test_framework.h"
#include "cmd_classify.h"
#include <string.h>

/* --- NULL and empty input --- */
int test_cmd_classify_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify(NULL, CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_empty(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* --- Linux safe commands --- */
int test_cmd_classify_linux_ls(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls -la /etc", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_cat(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat /etc/hostname", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_grep(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("grep -r 'foo' /var/log", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_ping(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping -c 4 8.8.8.8", CMD_PLATFORM_LINUX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("   ls -la", CMD_PLATFORM_LINUX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("/usr/bin/cat /etc/hosts", CMD_PLATFORM_LINUX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("cat /etc/hosts | grep localhost", CMD_PLATFORM_LINUX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("ls > /dev/null", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_redirect_stderr_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls 2>/dev/null", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_redirect_stderr_stdout(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls 2>&1", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* --- Subcommand sensitivity --- */
int test_cmd_classify_systemctl_status_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl status nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_sed_plain_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sed 's/foo/bar/' file.txt", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_sed_i_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("sed -i 's/foo/bar/' file.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_curl_plain_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("curl http://example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("mysql -e \"SELECT * FROM users\"", CMD_PLATFORM_LINUX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("show ip route", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_ios_ping_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping 10.0.0.1", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_ios_enable_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("enable", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("SHOW ip route", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    TEST_END();
}

/* --- Cisco NX-OS (IOS rules plus NX-OS extras) --- */
int test_cmd_classify_nxos_show_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show vlan", CMD_PLATFORM_CISCO_NXOS), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_CISCO_ASA), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("show ap database", CMD_PLATFORM_ARUBA_OS), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("show system info", CMD_PLATFORM_PANOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_panos_ping_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ping host 10.0.0.1", CMD_PLATFORM_PANOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_panos_commit_validate_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit validate", CMD_PLATFORM_PANOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_panos_test_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("test security-policy-match", CMD_PLATFORM_PANOS), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("request license info", CMD_PLATFORM_PANOS), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("show running-config | match ssl", CMD_PLATFORM_PANOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_panos_clear_session_all_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear session all", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Cross-platform heuristics (spec section 7.10) --- */
int test_cmd_classify_heuristic_show_always_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show version", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show version", CMD_PLATFORM_ARUBA_CX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show version", CMD_PLATFORM_PANOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_heuristic_negation_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no logging host 10.0.0.1", CMD_PLATFORM_CISCO_IOS) >= CMD_WRITE, 1);
    TEST_END();
}

int test_cmd_classify_heuristic_reload_always_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_heuristic_clear_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear counters", CMD_PLATFORM_CISCO_IOS) >= CMD_WRITE, 1);
    TEST_END();
}

/* ===================================================================
 * Command classification coverage expansion
 * (docs/superpowers/specs/2026-09-11-command-classification-coverage.md)
 * =================================================================== */

/* --- Linux additions: CRITICAL (spec 3.1) --- */

int test_cmd_classify_linux_critical_filesystem_destruction(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("fsck -y /dev/sda1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("e2fsck -f /dev/sdb1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("xfs_repair /dev/sdc1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("resize2fs /dev/sda1 20G", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("blkdiscard /dev/sdb", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("sgdisk --zap-all /dev/sdb", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("mdadm --stop /dev/md0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("cryptsetup luksFormat /dev/sdb1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("dmsetup remove mydevice", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("losetup -d /dev/loop0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_resource_removal(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("swapoff -a", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("umount /mnt/data", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("chroot /mnt/sysroot /bin/bash", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("kexec -e", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("telinit 0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_kernel_module(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("insmod /lib/modules/mymodule.ko", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("rmmod nvidia", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("modprobe -r nvidia", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_setenforce(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("setenforce 0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_firewall_wholesale(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("iptables-restore < /etc/iptables/rules.v4", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("nft delete table inet filter", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("nft -f /etc/nftables.conf", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ufw disable", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ufw reset", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("firewall-cmd --panic-on", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("firewall-cmd --complete-reload", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_systemctl_subcommands(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl poweroff", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("systemctl reboot", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("systemctl halt", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("systemctl isolate rescue.target", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("systemctl kill nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("systemctl set-default multi-user.target", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_service_stop(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("service nginx stop", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/etc/init.d/nginx stop", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("rc-service nginx stop", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("initctl stop nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* Package manager removal forms -- one per manager (spec 3.1 F5, corner case) */
int test_cmd_classify_linux_pkg_remove_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("apt remove nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("apt-get purge nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("dnf remove nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("yum erase nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("zypper rm nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("pacman -Rns nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("apk del nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("rpm -e nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("dpkg --purge nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("snap remove nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("flatpak uninstall org.mozilla.firefox", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("pip uninstall requests", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("npm uninstall -g eslint", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_pkg_remove_critical_extra(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("apt autoremove", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("apt-get autoremove", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("dnf autoremove", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("pacman -R nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("dpkg -r nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("dpkg -P nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("emerge --unmerge nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("emerge -C nginx", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* Package manager query forms -- one per manager (spec 3.3, corner case) */
int test_cmd_classify_linux_pkg_query_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("apt list --installed", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("apt-get upgrade --simulate", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dnf list installed", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("yum list installed", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("zypper search nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("pacman -Qi nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("apk info nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("rpm -qa", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dpkg -l", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("snap list", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("flatpak list", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("pip list", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("npm ls", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_critical_system_upgrade(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("apt-get dist-upgrade", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("dnf distro-sync", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("pacman -Syu", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("zypper dup", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("do-release-upgrade", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_account_removal(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("userdel -r bob", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("groupdel developers", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("deluser bob", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("passwd -d bob", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("passwd -l bob", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("usermod -L bob", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_container(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("docker stop mycontainer", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("docker kill mycontainer", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("docker rmi nginx:latest", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("docker system prune -a", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("docker volume rm mydata", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("docker network rm mynet", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("docker compose down", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("podman stop mycontainer", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("podman kill mycontainer", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_orchestration(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("kubectl drain node1 --ignore-daemonsets", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("kubectl cordon node1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("kubectl rollout undo deployment/myapp", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("kubectl replace --force -f pod.yaml", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("helm uninstall myrelease", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("helm rollback myrelease 1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_terraform(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("terraform destroy -auto-approve", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("terraform apply -auto-approve", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_network_iface(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("nmcli con down eth0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("nmcli con delete eth0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("nmcli device disconnect eth0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ifdown eth0", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("netplan apply", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ifconfig eth0 down", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_tc_netns(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("tc qdisc del dev eth0 root", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ip netns delete myns", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_find_delete(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("find / -name '*.tmp' -delete", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("find . -type f -exec rm -rf {} +", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_linux_critical_zfs_btrfs(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("zfs rollback tank/data@snapshot1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("btrfs subvolume delete /mnt/data/subvol1", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Linux additions: WRITE (spec 3.2) --- */

int test_cmd_classify_linux_write_files_and_archives(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("mount /dev/sdb1 /mnt/data", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("sysctl -w net.ipv4.ip_forward=1", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("chattr +i /etc/passwd", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("setfacl -m u:bob:rwx /data", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("visudo", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("groupmod -n newname oldname", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("chgrp staff /data/file", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("install -m 755 script.sh /usr/local/bin/script.sh", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("patch -p1 < fix.diff", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("unzip archive.zip -d /opt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("bunzip2 file.bz2", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("xz -d file.xz", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("7z x archive.7z", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_write_boot_and_system(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("logrotate -f /etc/logrotate.conf", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("update-grub", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("grub2-mkconfig -o /boot/grub2/grub.cfg", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dracut -f", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("update-initramfs -u", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ldconfig", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("timedatectl set-timezone UTC", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("hostnamectl set-hostname newhost", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("date -s \"2026-09-11 12:00:00\"", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ntpdate pool.ntp.org", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_write_pkg_install(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("apt update", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dnf install httpd", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("zypper in nginx", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("apk add curl", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("snap install core", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("flatpak install flathub org.gimp.GIMP", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("pip install requests", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("npm install express", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("gem install rails", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("cargo install ripgrep", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("go install golang.org/x/tools/cmd/goimports@latest", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_write_containers(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("docker start mycontainer", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("docker restart mycontainer", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("docker pull nginx:latest", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("docker tag nginx:latest myrepo/nginx:v1", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("docker commit mycontainer myimage:v1", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("docker compose up -d", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("podman start mycontainer", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("podman pull nginx:latest", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_write_orchestration_iac(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("helm install myrelease mychart", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("helm upgrade myrelease mychart", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("helm repo add stable https://charts.helm.sh/stable", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("terraform apply", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("terraform init", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ansible all -m ping", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ansible-playbook site.yml", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("kubectl annotate pod mypod key=value", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("kubectl label pod mypod env=prod", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("kubectl uncordon node1", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_linux_write_scheduling_net(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("crontab -e", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("at 10:00", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("batch", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("systemd-run --unit=myjob /bin/true", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("systemctl daemon-reload", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("nmcli con up eth0", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("nmcli con modify eth0 ipv4.method manual", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ip netns add myns", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("brctl addbr br0", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ovs-vsctl add-br br0", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("wg set wg0 peer ABC allowed-ips 0.0.0.0/0", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dmesg -C", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("journalctl --vacuum-time=2d", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("logger \"test message\"", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* --- Linux additions: SAFE (spec 3.3) --- */

int test_cmd_classify_linux_safe_readonly_tools(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("wc -l /etc/passwd", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("sort /etc/passwd", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("uniq /var/log/access.log", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("cut -d: -f1 /etc/passwd", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("diff file1.txt file2.txt", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("md5sum file.iso", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("sha256sum file.iso", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("file /bin/bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("stat /etc/passwd", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("readlink /etc/alternatives/editor", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("basename /usr/bin/bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("tree /etc", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("pwd", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("echo hello", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("uname -a", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("hostname", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("whoami", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("id", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ps aux", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("top -bn1", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("df -h", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("du -sh /var/log", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("lsblk", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_safe_network_tools(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("netstat -tulpn", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ss -tulpn", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ip addr show", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ip route show", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ip link show", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dig example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("nslookup example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("journalctl -u nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("iptables -L -n", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("iptables -S", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("nft list ruleset", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("firewall-cmd --list-all", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_safe_service_status(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl is-active nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("systemctl is-enabled nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("systemctl list-units", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("systemctl cat nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_safe_orchestration_status(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("docker ps -a", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("docker images", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("docker logs mycontainer", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("docker inspect mycontainer", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("kubectl get pods", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("kubectl describe pod mypod", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("helm list", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("helm status myrelease", CMD_PLATFORM_LINUX), (int)CMD_READ);
    /* terraform plan can invoke arbitrary provider code, so it is no longer
     * treated as flatly READ like "show"/"output" -- see the classifier
     * hardening pass (harden-part-b) and
     * test_cmd_classify_hardened_flags_raise_level below. */
    ASSERT_EQ((int)cmd_classify("terraform plan", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("terraform show", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* --- Corner case: ufw status vs ufw reset (spec 3.1 F8, section 17) --- */
int test_cmd_classify_linux_ufw_status_vs_reset(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ufw status", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ufw reset", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Cisco IOS additions (spec section 4) --- */

int test_cmd_classify_ios_safe_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("more flash:running-config.txt", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("where", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("who", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("reload cancel", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    TEST_END();
}

/* Corner case: configure replace -> CRITICAL (spec 4, F4) */
int test_cmd_classify_ios_configure_replace_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure replace flash:golden-config.cfg", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* Corner case: device-fs delete with flags before the path -> CRITICAL (spec 4, F3) */
int test_cmd_classify_ios_device_fs_delete_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("delete /force /recursive flash:c2800-image.bin", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("erase /force nvram:", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("format bootflash:", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("squeeze flash:", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_copy_into_config_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("copy tftp://10.1.1.1/backup.cfg running-config", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy tftp://10.1.1.1/backup.cfg startup-config", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_boot_system_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("boot system flash:c2960-universalk9.bin", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_default_interface_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("default interface GigabitEthernet0/1", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_no_forms_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no interface GigabitEthernet0/1", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no ip routing", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no username admin", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no aaa new-model", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no line vty 0 4", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no enable password", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no access-list 101", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no ip access-list extended BLOCK_TELNET", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no crypto map OUTSIDE_MAP", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no ip nat inside source list 1 interface GigabitEthernet0/0 overload", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no standby 1 ip 10.0.0.1", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no vrrp 1 ip 10.0.0.1", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no hsrp 1 ip 10.0.0.1", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_clear_adjacency_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear ip nat translation *", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear ip route *", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear bgp *", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear ip eigrp neighbors", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear isis *", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_hardware_reset_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("hw-module module 1 reset", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("hw-module slot 1 reload", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("microcode reload", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("redundancy reload shelf", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_install_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("install add file flash:cat9k_iosxe.bin", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install activate", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install commit", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install remove inactive", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request platform software package install switch 1 file flash:cat9k.bin", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_iosxr_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit replace", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("process restart bgp", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_crash_core_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("test crash", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("write core", CMD_PLATFORM_CISCO_IOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_ios_write_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("archive path flash:backup", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("license boot level ipservicesk9", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("event manager applet TEST", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("policy-map QOS_POLICY", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("class-map match-all VOICE", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("track 1 interface GigabitEthernet0/1 line-protocol", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("object-group network SERVERS", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("key chain KEY1", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("crypto isakmp policy 10", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("tunnel source GigabitEthernet0/1", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("vrf definition CUSTOMER_A", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ip sla 1", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("monitor session 1 source interface GigabitEthernet0/1", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("mac address-table static 0011.2233.4455 vlan 10 interface GigabitEthernet0/1", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("errdisable recovery cause bpduguard", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("power inline never", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("privilege exec level 5 show running-config", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("aaa authentication login default group tacacs+ local", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("tacacs-server host 10.0.0.5", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("radius-server host 10.0.0.6", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clock set 12:00:00 11 September 2026", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear line vty 0", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear arp-cache", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear mac address-table dynamic", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear logging", CMD_PLATFORM_CISCO_IOS), (int)CMD_WRITE);
    TEST_END();
}

/* --- Cisco NX-OS additions (spec section 5) --- */

int test_cmd_classify_nxos_critical_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("rollback running-config checkpoint mycheckpoint", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no feature bgp", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install activate", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install deactivate", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install remove", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("system switchover", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("out-of-service module 3", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("poweroff module 3", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("purge module 3", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("write erase boot", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("guestshell destroy", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("attach module 3", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("run bash", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot nxos bootflash:nxos.9.3.bin", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear ip route *", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no vlan 100", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no vrf context CUSTOMER_A", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no interface Ethernet1/1", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy tftp://10.0.0.1/config.txt running-config", CMD_PLATFORM_CISCO_NXOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_nxos_write_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("vdc CUSTOMER_A", CMD_PLATFORM_CISCO_NXOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("switchto vdc CUSTOMER_A", CMD_PLATFORM_CISCO_NXOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("guestshell enable", CMD_PLATFORM_CISCO_NXOS), (int)CMD_WRITE);
    TEST_END();
}

/* --- Cisco ASA additions (spec section 6) --- */

int test_cmd_classify_asa_critical_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("clear configure access-list", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear xlate", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear conn", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("crypto key zeroize rsa", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no crypto map OUTSIDE_MAP interface outside", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no access-group OUTSIDE_IN in interface outside", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no nat (inside,outside) source dynamic any interface", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no object-group network SERVERS", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no route outside 0.0.0.0 0.0.0.0 203.0.113.1", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no tunnel-group 203.0.113.5 type ipsec-l2l", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no username admin", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no aaa authentication ssh console LOCAL", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("failover reset", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no failover active", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot system disk0:/asa-image.bin", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot config disk0:/startup-config.cfg", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("upgrade software", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("hw-module module 1 reset", CMD_PLATFORM_CISCO_ASA), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_asa_write_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("shun 203.0.113.5", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("no shun 203.0.113.5", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("write standby", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear local-host all", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear crashinfo", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("perfmon interval 30", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("capture CAP interface outside", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dhcpd address 10.0.0.100-10.0.0.200 inside", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("webvpn", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ssl trust-point OUTSIDE-CERT outside", CMD_PLATFORM_CISCO_ASA), (int)CMD_WRITE);
    TEST_END();
}

/* --- HP ProCurve / ProVision (spec section 7, new platform) --- */

int test_cmd_classify_procurve_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ping 10.0.0.1", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("traceroute 10.0.0.1", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dir", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("menu", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("getmib sysDescr.0", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("walkmib system", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("exit", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("end", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("logout", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("page", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("terminal length 1000", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_procurve_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure terminal", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("write memory", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("vlan 100", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("interface 1", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("trunk 1-4 trk1 lacp", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ip address 10.0.0.1 255.255.255.0", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("hostname switch1", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("snmp-server community public operator", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("setmib sysContact.0 -s NOC", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("aaa authentication console login local", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("radius-server host 10.0.0.5", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("tacacs-server host 10.0.0.6", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("spanning-tree", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("password operator", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("time daylight-time-rule none", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("sntp server 10.0.0.10", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("logging 10.0.0.20", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("qos type-of-service diff-services", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("lldp enable", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("stack join member 2", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("kill 3", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("copy running-config tftp 10.0.0.50 backup.cfg", CMD_PLATFORM_HP_PROCURVE), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_procurve_critical_reboot_erase_copy(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot system flash primary", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("erase startup-config", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("erase all zeroize", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("erase flash", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("delete oldconfig.txt", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy tftp startup-config 10.0.0.50 backup.cfg", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy tftp flash 10.0.0.50 image.swi", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy xmodem flash", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy usb flash image.swi", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_procurve_critical_negation_disable(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no vlan 100", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no interface 1", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no ip routing", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no spanning-tree", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no router rip", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no password", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no aaa", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no snmp-server", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no password manager", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("password clear", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("interface 5 disable", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("factory-reset", CMD_PLATFORM_HP_PROCURVE), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- HPE Comware 5/7 (H3C) (spec section 8, new platform) --- */

int test_cmd_classify_comware_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("display current-configuration", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ping 10.0.0.1", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("tracert 10.0.0.1", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dir", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("more startup.cfg", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("quit", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("return", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("terminal monitor", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("screen-length 0 temporary", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_comware_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("system-view", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("save", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("save force", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("interface GigabitEthernet1/0/1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("vlan 100", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ip route-static 0.0.0.0 0 10.0.0.1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("acl number 3000", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ospf 1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("bgp 65000", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("stp enable", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("sysname Switch1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("local-user admin", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("nat static 10.0.0.1 203.0.113.1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("security-zone name Trust", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("security-policy ip", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("radius scheme RS1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("hwtacacs scheme HS1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("snmp-agent community read public", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("info-center loghost 10.0.0.5", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clock datetime 12:00:00 2026/09/11", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ntp-service unicast-server 10.0.0.10", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("irf-port 1/1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("qos wrr weight 5 5 5 5 5 5 5 5", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("port link-type trunk", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("lldp global enable", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("poe enable", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("debugging ip packet", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("reset counters interface GigabitEthernet1/0/1", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("reset logbuffer", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("undo dhcp enable", CMD_PLATFORM_HP_COMWARE), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_comware_critical_reset_reboot(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("schedule reboot delay 60", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset saved-configuration", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("restore factory-default", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("delete /unreserved flash:/startup.cfg", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("format flash:", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot-loader file flash:/comware7.bin", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("startup saved-configuration startup.cfg main", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("patch install flash:patch.bin", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install activate", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("install commit", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset bgp all", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset ospf 1 process", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset ip routing-table statistics", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset session all", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("irf member 2 renumber 3", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_comware_critical_undo(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("undo interface Vlan-interface100", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo vlan 100", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo local-user admin", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo ospf 1", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo bgp 65000", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo irf", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo stp", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo ip route-static 0.0.0.0 0 10.0.0.1", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo acl number 3000", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo security-policy ip", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo nat static 10.0.0.1 203.0.113.1", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_comware_critical_hidden_shell(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("_cmdline-mode on", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("xtd-cli-mode", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    TEST_END();
}

/* Corner case (spec section 17): identical literal "reboot" is CRITICAL on
 * Comware. F16 in the spec additionally claims it "classifies SAFE" under
 * the Linux ruleset to illustrate the F1 hazard (both call sites currently
 * hardcode CMD_PLATFORM_LINUX, so a Comware "reboot" run through the Linux
 * rules would not get the Comware-specific CRITICAL treatment) -- but the
 * Linux ruleset already lists bare "reboot" as CRITICAL in its own right
 * (see linux_critical_cmds in cmd_classify.c and
 * test_cmd_classify_linux_reboot above), so that half of F16's claim does
 * not hold for this literal command and is not asserted here. See this
 * agent's final report for the flag to the orchestrator. */
int test_cmd_classify_comware_reboot_vs_linux_reboot(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reboot", CMD_PLATFORM_HP_COMWARE), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Aruba OS-CX additions (spec section 9) --- */

int test_cmd_classify_aruba_cx_critical_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("boot", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot system", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reboot", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("start-shell", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no interface 1/1/1", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no vlan 100", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no vrf CUSTOMER_A", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no router bgp 65000", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no router ospf 1", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no router ospfv3 1", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no user admin", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no aaa authentication", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy tftp://10.0.0.1/config.json running-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy checkpoint startup running-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("vsx update-software", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear ip route all", CMD_PLATFORM_ARUBA_CX), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_aruba_cx_write_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("checkpoint auto", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("checkpoint post-configuration", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("copy running-config startup-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("banner motd \"Authorized use only\"", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ssh server vrf mgmt", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("https-server vrf mgmt", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("snmp-server community public", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("sflow enable", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("lag 1", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("lacp rate fast", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("spanning-tree", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("vrf CUSTOMER_A", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dhcp-server", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear arp", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear lldp neighbors", CMD_PLATFORM_ARUBA_CX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_aruba_cx_safe_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("checkpoint diff startup-config running-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("diff running-config startup-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("less running-config", CMD_PLATFORM_ARUBA_CX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("top", CMD_PLATFORM_ARUBA_CX), (int)CMD_READ);
    TEST_END();
}

/* --- ArubaOS (controller / Instant) additions (spec section 10) --- */

int test_cmd_classify_aruba_os_critical_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("halt", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("disable-ap", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("crypto-local isakmp zeroize", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("copy tftp://10.0.0.1/ArubaOS.img system: partition 1", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear datapath session", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear gap-db", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("convert-aos-ap", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("local-userdb del username bob", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("license del ABCD1234", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no aaa profile corp-profile", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no wlan ssid-profile corp-ssid", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("no user-role guest", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("write erase all", CMD_PLATFORM_ARUBA_OS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_aruba_os_write_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ap-rename AP01 AP02", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ap-regroup AP01 group-new", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("provision-ap", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("database synchronize", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("local-userdb add username bob", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("license add ABCD1234", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("upgrade-profile", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("master-redundancy", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("papi-security", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("firewall session-idle-timeout 900", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("ids", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("wids", CMD_PLATFORM_ARUBA_OS), (int)CMD_WRITE);
    TEST_END();
}

/* --- PAN-OS additions (spec section 11) --- */

/* Corner case: "set cli config-output-format set" -> SAFE (spec 11, F15, section 17) */
int test_cmd_classify_panos_set_cli_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("set cli config-output-format set", CMD_PLATFORM_PANOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("set cli pager off", CMD_PLATFORM_PANOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_panos_safe_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request support info", CMD_PLATFORM_PANOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("check", CMD_PLATFORM_PANOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("debug show routing", CMD_PLATFORM_PANOS), (int)CMD_READ);
    TEST_END();
}

/* Corner case: PAN-OS spells the dry run "commit validate"; "commit check" is
 * Junos syntax, not PAN-OS, so it must fail closed as CRITICAL rather than be
 * waved through as a validation (spec 11, section 17). */
int test_cmd_classify_panos_commit_check_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit validate", CMD_PLATFORM_PANOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("commit check", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_critical_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("set deviceconfig system ip-address 10.0.0.1", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("set deviceconfig system netmask 255.255.255.0", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("set deviceconfig system default-gateway 10.0.0.1", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request plugins vm_series install", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request restart dataplane", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request ha state suspend", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request global-protect-gateway client-logout gateway GW1", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request vpn ipsec-sa clear tunnel VPN1", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request vpn ike-sa clear tunnel VPN1", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear dos-block-table", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("load named-configuration backup.xml", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

int test_cmd_classify_panos_write_additions(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request content upgrade download latest", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("request anti-virus upgrade download latest", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("request url-filtering update", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("request system external-list refresh name blocklist", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("save named-configuration snapshot pre-change.xml", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("revert config", CMD_PLATFORM_PANOS), (int)CMD_WRITE);
    TEST_END();
}

/* --- Juniper Junos (spec section 12, new platform) --- */

int test_cmd_classify_junos_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show interfaces terse", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("run show route", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ping 10.0.0.1", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("traceroute 10.0.0.1", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("monitor traffic interface ge-0/0/0", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("monitor interface traffic", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("file show /var/log/messages", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("file list /var/tmp", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("file compare files a.conf b.conf", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("compare", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("help apropos commit", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("exit", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("quit", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("top", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("up", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("commit check", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("test policy from-zone trust to-zone untrust policy allow-web match-source-address any", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("request support information", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_junos_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("edit interfaces ge-0/0/0", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("set interfaces ge-0/0/0 unit 0 family inet address 10.0.0.1/24", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("deactivate interfaces ge-0/0/0", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("activate interfaces ge-0/0/0", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("rename interfaces ge-0/0/0 to ge-0/0/1", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("copy interfaces ge-0/0/0 to ge-0/0/1", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("insert interfaces ge-0/0/0 before ge-0/0/1", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("annotate interfaces ge-0/0/0 \"uplink\"", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("replace pattern old-string with new-string", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("load merge terminal", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("load set terminal", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("load patch /var/tmp/patch.txt", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("save /var/tmp/config-backup.conf", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("rollback 1", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("commit confirmed 5", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("request system snapshot", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("request system license add terminal", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("file copy /var/tmp/a.conf /var/tmp/b.conf", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("file archive compress source /var/log destination /var/tmp/logs.tgz", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("op myscript.slax", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear interfaces statistics ge-0/0/0", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear log messages", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear arp", CMD_PLATFORM_JUNOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_junos_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("commit and-quit", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("commit synchronize", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("commit force", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("commit at \"2026-09-12 02:00:00\"", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("delete interfaces ge-0/0/0", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("load override /var/tmp/full-config.conf", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("load replace /var/tmp/partial-config.conf", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("load factory-default", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request system reboot", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request system halt", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request system power-off", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request system zeroize", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request system software add /var/tmp/junos-install.tgz", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request system partition", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request chassis cluster failover redundancy-group 1 node 1", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("restart routing", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("restart dhcp-service", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear bgp neighbor 10.0.0.1", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear ospf neighbor", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear isis adjacency", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear security flow session all", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("clear system commit", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("file delete /var/tmp/old-config.conf", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("start shell", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- Fortinet FortiOS (spec section 13, new platform) --- */

int test_cmd_classify_fortios_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("get system status", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show full-configuration", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("end", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("next", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("abort", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("exit", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("execute ping 10.0.0.1", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("execute traceroute 10.0.0.1", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("execute telnet 10.0.0.1", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("execute ssh admin@10.0.0.1", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("execute time", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("diagnose sys top", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("diagnose debug enable", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("diagnose sniffer packet any \"host 10.0.0.1\" 4", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_fortios_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("config firewall policy", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("edit 1", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("set srcintf \"port1\"", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("unset comments", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("append member \"8.8.8.8\"", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clone 1 to 2", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("move 3 after 1", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("rename myobject to newobject", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("execute backup config flash", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("execute update-now", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("execute date 2026-09-11", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("execute ha manage 0 admin", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("execute cli set-cli-timeout 3600", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("diagnose sys session list", CMD_PLATFORM_FORTIOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_fortios_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("delete 1", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("purge", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute factoryreset", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute factoryreset2", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute erase-disk", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute formatlogdisk", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute reboot", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute shutdown", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute restore config flash backup.cfg", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute restore image flash primary", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute ha failover set", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute ha synchronize", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute log delete-all", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute router clear bgp all", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute router clear ospf process", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute disconnect-admin-session admin", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("diagnose sys kill 9 1234", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("diagnose hardware test disk read", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("diagnose sys flash format", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("diagnose vpn tunnel down VPN1", CMD_PLATFORM_FORTIOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- VyOS (spec section 14, new platform) --- */

int test_cmd_classify_vyos_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show interfaces", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("run show version", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("compare", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ping 10.0.0.1", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("traceroute 10.0.0.1", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("monitor interface ethernet eth0 traffic", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("exit", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("top", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("up", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("commit-check", CMD_PLATFORM_VYOS), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_vyos_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("configure", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("set interfaces ethernet eth0 address 10.0.0.1/24", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("save", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("discard", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("comment interfaces ethernet eth0 \"uplink\"", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("rename interfaces ethernet eth0 to eth1", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("copy interfaces ethernet eth0 to interfaces ethernet eth1", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("commit-confirm 10", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("generate ssh-client-key", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("add system image /var/tmp/vyos-1.4.img", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("clear interfaces counters", CMD_PLATFORM_VYOS), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_vyos_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("delete interfaces ethernet eth0", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("load /config/config.boot", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("merge /config/backup-config.boot", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reboot", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("poweroff", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset vpn ipsec-peer 203.0.113.5", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset ip bgp 10.0.0.1", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset ospf process", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("delete system image vyos-1.3", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("set system image default-boot vyos-1.4", CMD_PLATFORM_VYOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* --- MikroTik RouterOS (spec section 15, new platform) --- */

int test_cmd_classify_mikrotik_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/ip firewall filter print", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("/interface ethernet get [find default-name=ether1]", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("/system identity export", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("/ip route find", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("/tool monitor-traffic interface=ether1", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("/system routerboard print", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_mikrotik_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/ip address add address=10.0.0.1/24 interface=ether1", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("/interface ethernet set ether1 comment=uplink", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("/system script edit test source", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("/queue simple move 0 destination=1", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("/interface ethernet enable ether1", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("/tool fetch url=\"http://example.com/file.npk\"", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("/ip firewall filter import file-name=rules.rsc", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("/system backup save name=backup1", CMD_PLATFORM_MIKROTIK), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_mikrotik_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/ip firewall filter remove numbers=0", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system reset-configuration no-defaults=yes", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system reset", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system reboot", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system shutdown", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/interface ethernet disable ether1", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system routerboard upgrade", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system package downgrade", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system package upgrade", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/disk format-drive 0", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/system backup load name=backup1", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    TEST_END();
}

/* Order matters: CRITICAL checked before SAFE, so a segment containing both
 * "remove" and "find" is CRITICAL, not SAFE (spec section 15, last paragraph). */
int test_cmd_classify_mikrotik_critical_wins_over_find(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/ip firewall filter remove [find disabled=yes]", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    TEST_END();
}

/* Corner case (section 17): verb-last SAFE vs CRITICAL pair, asserted together */
int test_cmd_classify_mikrotik_print_vs_remove(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/ip firewall filter print", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("/ip firewall filter remove numbers=0", CMD_PLATFORM_MIKROTIK), (int)CMD_CRITICAL);
    TEST_END();
}

/* ===================================================================
 * Section 17 corner cases that cut across platforms
 * =================================================================== */

/* display filter is a filter, not a pipe, on every network platform (F2) */
int test_cmd_classify_display_filter_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_CISCO_NXOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_CISCO_ASA), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_ARUBA_CX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_ARUBA_OS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_comware_display_filter_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("display current-configuration | include ospf", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    TEST_END();
}

/* commit check (Junos) / commit validate (PAN-OS) stay SAFE; bare commit is
 * CRITICAL on both (spec 11, 12, section 17) */
int test_cmd_classify_commit_check_validate_vs_bare_commit(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("commit check", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("commit validate", CMD_PLATFORM_PANOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_JUNOS), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_PANOS), (int)CMD_CRITICAL);
    TEST_END();
}

/* Empty string and a lone separator classify SAFE for every new platform
 * (spec section 17) */
int test_cmd_classify_new_platforms_empty_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_new_platforms_lone_separator_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify(";", CMD_PLATFORM_HP_PROCURVE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify(";", CMD_PLATFORM_HP_COMWARE), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify(";", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify(";", CMD_PLATFORM_FORTIOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify(";", CMD_PLATFORM_VYOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify(";", CMD_PLATFORM_MIKROTIK), (int)CMD_READ);
    TEST_END();
}

/* ===== CMD_PLATFORM_UNKNOWN overlay (platform-plumbing spec section 2) =====
 * classify_unknown_segment() claims a short list of first-token network
 * verbs as CRITICAL/WRITE and delegates everything else to
 * classify_linux_segment() unchanged. */

/* Unconditional first-token verbs -> CRITICAL */
int test_cmd_classify_unknown_overlay_critical_verbs(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reboot", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("erase startup-config", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("factory-reset", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("restore", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("factory-default", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("rollback", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("delete flash:old.bin", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("purge", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("undo", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("boot", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    TEST_END();
}

/* "write erase" is claimed; a bare "write" (no "erase" second token) is not
 * a network verb the overlay recognises and falls through to Linux, where
 * "write" is not a real Linux command either.
 *
 * CHANGED (UNKNOWN safety category, C2 fix): "write memory" used to assert
 * CMD_READ here because an unrecognised Linux command fell through to SAFE.
 * That fallthrough is now CMD_UNKNOWN (classify_linux_segment() reaches SAFE
 * only via its explicit allow-list, and "write" is not on it), so the
 * CMD_PLATFORM_UNKNOWN overlay -- which just delegates -- now honestly
 * reports CMD_UNKNOWN too. Old -> CMD_READ, new -> CMD_UNKNOWN. */
int test_cmd_classify_unknown_overlay_write_erase(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("write erase", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("write memory", CMD_PLATFORM_UNKNOWN), (int)CMD_UNKNOWN);
    TEST_END();
}

/* "request system/restart/shutdown" is claimed; other "request ..." forms
 * fall through to Linux, where "request" is not a real command either.
 *
 * CHANGED (UNKNOWN safety category, C2 fix): "request support info" used to
 * assert CMD_READ for the same reason as "write memory" above -- an
 * unrecognised Linux command is now CMD_UNKNOWN, not SAFE. Old -> CMD_READ,
 * new -> CMD_UNKNOWN. */
int test_cmd_classify_unknown_overlay_request_verbs(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("request system reboot", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request restart system", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request shutdown system", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("request support info", CMD_PLATFORM_UNKNOWN), (int)CMD_UNKNOWN);
    TEST_END();
}

/* "execute factoryreset/reboot/restore" is claimed; other "execute ..."
 * forms fall through to Linux, where "execute" is not a real command either.
 *
 * CHANGED (UNKNOWN safety category, C2 fix): "execute ping 8.8.8.8" used to
 * assert CMD_READ for the same reason as above -- an unrecognised Linux
 * command is now CMD_UNKNOWN, not SAFE. Old -> CMD_READ, new ->
 * CMD_UNKNOWN. */
int test_cmd_classify_unknown_overlay_execute_verbs(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("execute factoryreset", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute reboot", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute restore config", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("execute ping 8.8.8.8", CMD_PLATFORM_UNKNOWN), (int)CMD_UNKNOWN);
    TEST_END();
}

/* First-token verbs -> WRITE */
int test_cmd_classify_unknown_overlay_write_verbs(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("no shutdown", CMD_PLATFORM_UNKNOWN), (int)CMD_WRITE);
    /* The overlay may only raise, never lower: "shutdown" is a config verb
     * on a network CLI but CRITICAL on a Linux host, and an unresolved
     * session must not classify below what the Linux ruleset alone says. */
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("shutdown", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("configure terminal", CMD_PLATFORM_UNKNOWN), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("system-view", CMD_PLATFORM_UNKNOWN), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("set foo bar", CMD_PLATFORM_UNKNOWN), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("config t", CMD_PLATFORM_UNKNOWN), (int)CMD_WRITE);
    TEST_END();
}

/* Everything the overlay doesn't claim delegates to classify_linux_segment()
 * unchanged -- proving the Linux path is intact under CMD_PLATFORM_UNKNOWN. */
int test_cmd_classify_unknown_linux_safe_commands_intact(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls -la", CMD_PLATFORM_UNKNOWN), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("cat /etc/hosts", CMD_PLATFORM_UNKNOWN), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("grep -r x /var/log", CMD_PLATFORM_UNKNOWN), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_unknown_linux_critical_command_intact(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("rm -rf /", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    TEST_END();
}

/* ===== Platform name / label mapping (platform-plumbing spec section 4.1) ===== */

int test_cmd_platform_from_name_round_trip(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_platform_from_name("linux"), (int)CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)cmd_platform_from_name("cisco-ios"), (int)CMD_PLATFORM_CISCO_IOS);
    ASSERT_EQ((int)cmd_platform_from_name("cisco-nxos"), (int)CMD_PLATFORM_CISCO_NXOS);
    ASSERT_EQ((int)cmd_platform_from_name("cisco-asa"), (int)CMD_PLATFORM_CISCO_ASA);
    ASSERT_EQ((int)cmd_platform_from_name("hp-procurve"), (int)CMD_PLATFORM_HP_PROCURVE);
    ASSERT_EQ((int)cmd_platform_from_name("hp-comware"), (int)CMD_PLATFORM_HP_COMWARE);
    ASSERT_EQ((int)cmd_platform_from_name("aruba-cx"), (int)CMD_PLATFORM_ARUBA_CX);
    ASSERT_EQ((int)cmd_platform_from_name("aruba-os"), (int)CMD_PLATFORM_ARUBA_OS);
    ASSERT_EQ((int)cmd_platform_from_name("panos"), (int)CMD_PLATFORM_PANOS);
    ASSERT_EQ((int)cmd_platform_from_name("junos"), (int)CMD_PLATFORM_JUNOS);
    ASSERT_EQ((int)cmd_platform_from_name("fortios"), (int)CMD_PLATFORM_FORTIOS);
    ASSERT_EQ((int)cmd_platform_from_name("vyos"), (int)CMD_PLATFORM_VYOS);
    ASSERT_EQ((int)cmd_platform_from_name("routeros"), (int)CMD_PLATFORM_MIKROTIK);
    TEST_END();
}

int test_cmd_platform_name_round_trip(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_LINUX), "linux");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_CISCO_IOS), "cisco-ios");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_CISCO_NXOS), "cisco-nxos");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_CISCO_ASA), "cisco-asa");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_HP_PROCURVE), "hp-procurve");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_HP_COMWARE), "hp-comware");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_ARUBA_CX), "aruba-cx");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_ARUBA_OS), "aruba-os");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_PANOS), "panos");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_JUNOS), "junos");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_FORTIOS), "fortios");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_VYOS), "vyos");
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_MIKROTIK), "routeros");
    /* CMD_PLATFORM_UNKNOWN is the unresolved/awaiting-detection state, and
     * "auto" -- not a device family -- is what it persists as. */
    ASSERT_STR_EQ(cmd_platform_name(CMD_PLATFORM_UNKNOWN), "auto");
    TEST_END();
}

int test_cmd_platform_from_name_unknown_token(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_platform_from_name("not-a-real-platform"), (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)cmd_platform_from_name("Cisco-IOS-XR"), (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)cmd_platform_from_name(""), (int)CMD_PLATFORM_UNKNOWN);
    TEST_END();
}

int test_cmd_platform_from_name_null(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_platform_from_name(NULL), (int)CMD_PLATFORM_UNKNOWN);
    TEST_END();
}

/* "auto" is not a device family -- it means "no override, detect it" -- and
 * maps to CMD_PLATFORM_UNKNOWN, the correct starting state for a session
 * awaiting detection. */
int test_cmd_platform_from_name_auto(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_platform_from_name("auto"), (int)CMD_PLATFORM_UNKNOWN);
    ASSERT_EQ((int)cmd_platform_from_name("AUTO"), (int)CMD_PLATFORM_UNKNOWN);
    TEST_END();
}

int test_cmd_platform_label_unknown_is_auto_detect(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(cmd_platform_label(CMD_PLATFORM_UNKNOWN), "Auto-detect");
    ASSERT_STR_EQ(cmd_platform_label(CMD_PLATFORM_CISCO_IOS), "Cisco IOS / IOS-XE");
    ASSERT_STR_EQ(cmd_platform_label(CMD_PLATFORM_MIKROTIK), "MikroTik RouterOS");
    TEST_END();
}

/* The overlay also has to cover the two shapes whose first token identifies
   nothing: Comware's two-token "reset ..." (a bare "reset" is the harmless
   Linux terminal reset) and RouterOS's verb-last "/path ... verb" form. */
int test_cmd_classify_unknown_overlay_reset_forms(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("reset saved-configuration", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset bgp all", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("reset ospf process", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    /* bare "reset" is the terminfo terminal reset on a Linux host */
    ASSERT_EQ((int)cmd_classify("reset", CMD_PLATFORM_UNKNOWN), (int)CMD_READ);
    TEST_END();
}

/* CHANGED (UNKNOWN safety category, C2 fix): "/ip firewall filter print"
 * used to assert CMD_READ. The RouterOS-verb-last overlay above only claims
 * a short critical-verb list for "/..." segments (none of which is "print"),
 * so this falls through to classify_linux_segment(), which strips the
 * leading path down to base command "ip" -- a real but subcommand-sensitive
 * Linux command whose "firewall" token matches no rule. That used to fall
 * through to the old SAFE bug; it is now honestly CMD_UNKNOWN, same as any
 * other unrecognised Linux invocation. The absolute-path Linux command below
 * is unaffected: it strips to base command "uptime", which is a genuine
 * Linux allow-list entry. Old -> CMD_READ, new -> CMD_UNKNOWN. */
int test_cmd_classify_unknown_overlay_routeros_paths(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("/system reset-configuration", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/ip firewall filter remove numbers=0", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("/interface disable ether1", CMD_PLATFORM_UNKNOWN), (int)CMD_CRITICAL);
    /* a Linux absolute-path invocation of a real allow-listed command must
     * stay out of the way */
    ASSERT_EQ((int)cmd_classify("/ip firewall filter print", CMD_PLATFORM_UNKNOWN), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("/usr/bin/uptime", CMD_PLATFORM_UNKNOWN), (int)CMD_READ);
    TEST_END();
}

/* ===================================================================
 * CMD_UNKNOWN safety category (2026-09-11-unknown-safety-category-design.md
 * section 3): Linux SAFE becomes an allow-list, and every network platform's
 * "conservative -> CMD_WRITE" fallthrough becomes CMD_UNKNOWN.
 * =================================================================== */

/* An unrecognised Linux command is CMD_UNKNOWN, not SAFE -- this is the C2
 * fix in its most direct form: classify_linux_segment() only reaches SAFE
 * by matching an explicit rule now. */
int test_cmd_classify_linux_unrecognised_is_unknown(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate --deeply -x", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    TEST_END();
}

/* A redirect still raises an unrecognised command to WRITE: the write comes
 * from the shell redirect itself, not from recognising "frobnicate". */
int test_cmd_classify_linux_unrecognised_with_redirect_is_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("frobnicate > /etc/passwd", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* "cat x > y" is WRITE (the redirect beats the allow-list); "cat x" alone
 * is SAFE (spec section 3, "Redirects still raise"). */
int test_cmd_classify_linux_allow_list_vs_redirect(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cat x > y", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("cat x", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* An allow-listed command that a write/critical rule also claims keeps the
 * higher level: the allow-list is consulted last, not first (spec section
 * 3). "tar" is on the write list; "find ... -delete" is caught by the
 * critical scan before either list is reached. */
int test_cmd_classify_linux_allow_list_does_not_downgrade_write_or_critical(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("tar czf backup.tar.gz /etc", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("find / -delete", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    TEST_END();
}

/* New single-token linux_read_cmds[] entries (coverage spec 3.3), grouped
 * roughly as the spec groups them. Every category of the section 3
 * allow-list gets at least one assertion here. */
int test_cmd_classify_linux_safe_allow_list_file_and_text_tools(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("tac /var/log/syslog", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("less /etc/passwd", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("tail -f /var/log/syslog", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("egrep 'foo|bar' file.txt", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("fgrep literal file.txt", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("rg pattern .", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("column -t file.txt", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("cmp a.txt b.txt", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("sha1sum file.iso", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("realpath ./file", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dirname /usr/bin/bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("printf 'hi\\n'", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("true", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("false", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_safe_allow_list_system_inspectors(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cal", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("w", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("who", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("groups", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("lsb_release -a", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("arch", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("nproc", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("free -h", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("vmstat 1", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("iostat", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("mpstat", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("sar -u", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("pidstat", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("pstree", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("htop", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("blkid", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("findmnt", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("lsof -i", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("lspci", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("lsusb", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("lscpu", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("lsmod", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dmidecode", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("sensors", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("getenforce", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("sestatus", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("env", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("printenv PATH", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("locale", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("which bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("whereis bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("type bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("man ls", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("history", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

int test_cmd_classify_linux_safe_allow_list_network_diagnostics(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("arp -a", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ping6 ::1", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("tracepath example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("mtr example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("host example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("whois example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* Multi-token allow-list entries that don't fit the single-token
 * linux_read_cmds[] table and so were added to linux_subcmd_rules
 * instead. */
int test_cmd_classify_linux_safe_allow_list_multitoken(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("systemctl cat nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("systemctl show nginx", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("systemctl list-timers", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("nft list ruleset", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ip neigh show", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("route -n", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("docker stats --no-stream", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("docker top mycontainer", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("kubectl top pods", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("kubectl explain pod.spec", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("kubectl api-resources", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("kubectl version", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* rpm -q* / pacman -Q*: any query subcommand beyond the exact -qa/-qi/-ql
 * and -Q/-Qs/-Qi forms is still read-only. */
int test_cmd_classify_linux_safe_rpm_pacman_query_prefix(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("rpm -qf /bin/bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("rpm -qc bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("pacman -Qo /bin/bash", CMD_PLATFORM_LINUX), (int)CMD_READ);
    /* a non-query rpm/pacman invocation is still flat WRITE by default */
    ASSERT_EQ((int)cmd_classify("rpm -ivh package.rpm", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* "iptables-save" is its own binary, distinct from "iptables -S" (already
 * covered by linux_subcmd_rules); both are read-only. */
int test_cmd_classify_linux_safe_iptables_save(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("iptables-save", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* "date", "hostname", "dmesg" and "mount" are safe only bare -- any argument
 * takes them off the blanket allow-list (spec 3.3's "*(bare)*" entries).
 * "mount" already had its own bare-check before this change; the others are
 * new. */
int test_cmd_classify_linux_bare_only_safe_forms(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("date", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("date -s \"2026-09-11 12:00:00\"", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    /* date, hostname and dmesg now have flag allow-lists: a format
     * operand and the display flags are READ, anything that sets state is
     * WRITE, an unlisted flag is UNKNOWN. */
    ASSERT_EQ((int)cmd_classify("date +%Y-%m-%d", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("date -u", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("date 010100002030", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("date --se=x", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("date -Z", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);

    ASSERT_EQ((int)cmd_classify("hostname", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("hostname -I", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("hostname -f", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("hostname newname", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("hostname -F F", CMD_PLATFORM_LINUX), (int)CMD_WRITE);

    ASSERT_EQ((int)cmd_classify("dmesg", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dmesg -C", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dmesg -TC", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dmesg --cl", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dmesg -T", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dmesg --level=err", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dmesg -Q", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);

    ASSERT_EQ((int)cmd_classify("mount", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("mount /dev/sdb1 /mnt/data", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* An unrecognised command on every network platform is CMD_UNKNOWN, not the
 * old "conservative" CMD_WRITE guess (spec section 3, covers IOS, NX-OS,
 * ASA, ProCurve, Comware, Aruba CX, ArubaOS, PAN-OS, Junos, FortiOS, VyOS,
 * MikroTik). */
int test_cmd_classify_network_platforms_unrecognised_is_unknown(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_CISCO_IOS), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_CISCO_NXOS), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_CISCO_ASA), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_ARUBA_CX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_ARUBA_OS), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_PANOS), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_HP_PROCURVE), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_HP_COMWARE), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_JUNOS), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_FORTIOS), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_VYOS), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("frobnicate", CMD_PLATFORM_MIKROTIK), (int)CMD_UNKNOWN);
    TEST_END();
}

/* The F2 display-filter fix (M1: "|" is a filter, not a pipe, on every
 * platform but Linux) still holds with the new fallthrough -- a recognised
 * "show ... | include ..." line must not be dragged down to CMD_UNKNOWN by
 * treating "include ospf" as a second, unrecognised segment. */
int test_cmd_classify_network_display_filter_still_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", CMD_PLATFORM_JUNOS), (int)CMD_READ);
    TEST_END();
}

/* ===== cmd_classify_mask() (design spec section 4 / section 8) =====
 * A pipeline's mask is the set of categories across all its segments, not
 * just the maximum -- this is what lets the auto-approve gate refuse a
 * pipeline containing an unknown segment even when the pipeline's overall
 * maximum would otherwise be permitted. */

int test_cmd_classify_mask_all_safe_pipeline(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify_mask("ls | grep x", CMD_PLATFORM_LINUX), CMD_MASK_OF(CMD_READ));
    TEST_END();
}

/* {SAFE, UNKNOWN}: the unrecognised first segment must show up in the mask
 * even though the pipeline's plain maximum (via cmd_classify()) is also
 * just CMD_UNKNOWN here -- this is the easy case before the harder one
 * below. */
int test_cmd_classify_mask_unknown_and_safe_pipeline(void) {
    TEST_BEGIN();
    unsigned mask = cmd_classify_mask("frobnicate | grep x", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)mask, (int)(CMD_MASK_OF(CMD_READ) | CMD_MASK_OF(CMD_UNKNOWN)));
    ASSERT_EQ((int)cmd_classify("frobnicate | grep x", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    TEST_END();
}

/* {UNKNOWN, WRITE}, maximum WRITE: the case the mask exists for. Collapsing
 * to a maximum alone would lose the fact that the pipeline also contains an
 * unrecognised segment -- under a "safe + write" auto-approve mode that
 * excludes unknown, this pipeline must not auto-approve just because its
 * maximum is a permitted category (design spec section 4). */
int test_cmd_classify_mask_unknown_and_write_pipeline(void) {
    TEST_BEGIN();
    unsigned mask = cmd_classify_mask("frobnicate | tee /etc/f", CMD_PLATFORM_LINUX);
    ASSERT_EQ((int)mask, (int)(CMD_MASK_OF(CMD_UNKNOWN) | CMD_MASK_OF(CMD_WRITE)));
    ASSERT_EQ((int)cmd_classify("frobnicate | tee /etc/f", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_mask_null_and_empty(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify_mask(NULL, CMD_PLATFORM_LINUX), CMD_MASK_OF(CMD_READ));
    ASSERT_EQ((int)cmd_classify_mask("", CMD_PLATFORM_LINUX), CMD_MASK_OF(CMD_READ));
    TEST_END();
}

/* ===== Cases inherited from the old ai_command_is_readonly() suite =====
 * ai_command_is_readonly() was a boolean wrapper that asked cmd_classify()
 * with a hardcoded CMD_PLATFORM_LINUX. It had no callers and was deleted;
 * these are the cases its tests covered that nothing here did, restated
 * against cmd_classify() directly so the coverage survives the removal. */

/* Interactive editors and ownership changes are writes: they are not on the
 * read-only allow-list and must never be mistaken for it. */
int test_cmd_classify_linux_editors_and_chown_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("touch newfile.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("vim file.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("nano file.txt", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("chown root:root file", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* Redirect forms the existing redirect tests above do not spell out: the
 * `&>` both-streams form, and `2>` with a space before the target. Both go
 * to /dev/null, so both stay SAFE. */
int test_cmd_classify_redirect_devnull_spacing_and_ampersand(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls &>/dev/null", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("find / -name '*.log' 2> /dev/null", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* The same `2>` spacing against a real file is still a write -- the space is
 * not what makes /dev/null harmless, the target is. The leading command is
 * unrecognised here, which makes the point sharper: the redirect alone
 * carries it past UNKNOWN to WRITE. */
int test_cmd_classify_redirect_stderr_to_real_file_write(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("cmd 2> errors.log", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    TEST_END();
}

/* The exact command from the original user bug report: a long read-only
 * pipeline whose `2>/dev/null` must not drag it out of SAFE. This is the
 * regression the harmless-redirect handling exists for. */
int test_cmd_classify_linux_readonly_du_pipeline_regression(void) {
    TEST_BEGIN();
    /* find -exec now always floors at UNKNOWN even when the executed
     * command (here "du") is itself a read command -- find -exec runs an
     * arbitrary program per match, which "du" happening to be safe here
     * does not make provably safe in general. See the classifier hardening
     * pass (harden-part-b) and test_cmd_classify_hardened_flags_raise_level
     * below ("find -exec cat ... -> at least UNKNOWN"). */
    ASSERT_EQ((int)cmd_classify(
        "find ~ -type f -exec du -h {} + 2>/dev/null | sort -rh | head -20",
        CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("du -sh /* 2>/dev/null | sort -rh",
        CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* A network-device command classified against the Linux ruleset is
 * CMD_UNKNOWN, not SAFE and not CRITICAL: the wrong ruleset cannot vouch for
 * it either way. This holds for the read-only verbs (`show`, `display`) as
 * much as the destructive ones -- under Linux rules a SAFE answer would be a
 * guess. Classified against their own platform they come out SAFE and
 * CRITICAL respectively, which the per-platform tests above cover.
 *
 * Before the UNKNOWN category existed these all fell through to SAFE
 * (security audit C2) -- that is why `reload` used to auto-approve on a
 * switch. The sibling test above pins the same behaviour for
 * CMD_PLATFORM_UNKNOWN; this one pins it for a session explicitly set to
 * Linux. */
int test_cmd_classify_network_verbs_under_linux_are_unknown(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("show running-config", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("show interfaces", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("display version", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("configure terminal", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("conf t", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("configure", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("write memory", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("commit", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("reload", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("rollback", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("erase startup-config", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("execute reboot", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    TEST_END();
}

/* ===== PowerShell as a custom local shell =====
 * A PowerShell local session stays on CMD_PLATFORM_UNKNOWN (no banner or
 * prompt resolves it), so its commands meet the Linux ruleset plus the
 * network-verb overlay. No cmdlet is known to that ruleset: every one comes
 * out UNKNOWN (gated like a write, never auto-approved by a read-only
 * policy), never READ. The few PowerShell aliases that are also Linux
 * command names keep their Linux answer. */
int test_cmd_classify_powershell_cmdlets_under_unknown(void) {
    TEST_BEGIN();
    const CmdPlatform u = CMD_PLATFORM_UNKNOWN;
    ASSERT_EQ((int)cmd_classify("Get-ChildItem", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("Get-Content x", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("ls", u), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("Remove-Item -Recurse -Force C:\\temp\\x", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("rm -r x", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("Stop-Process -Name foo", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("Set-Content x y", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("Format-Volume -DriveLetter D", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("Restart-Computer", u), (int)CMD_UNKNOWN);
    TEST_END();
}

/* A downloaded script piped into Invoke-Expression is PowerShell's
 * `curl | sh`: CRITICAL as a pipe target, like sh and bash, on Linux and on
 * an unresolved platform alike, and in any letter case (PowerShell ignores
 * it). iex/Invoke-Expression as the command itself -- not just a pipe
 * target -- is also CRITICAL as of v1.2.9 (classifier holes audit): it
 * executes a string as code regardless of where that string came from,
 * including the call-operator-glued form "IEX(...)" with no space. */
int test_cmd_classify_pipe_into_invoke_expression_is_critical(void) {
    TEST_BEGIN();
    const CmdPlatform u = CMD_PLATFORM_UNKNOWN;
    const CmdPlatform l = CMD_PLATFORM_LINUX;
    ASSERT_EQ((int)cmd_classify("Invoke-WebRequest https://example.com/s.ps1 | Invoke-Expression", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("Invoke-WebRequest https://example.com/s.ps1 | Invoke-Expression", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("iwr https://example.com/s.ps1 | iex", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl https://example.com/s.ps1 | iex", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl https://example.com/s.ps1 | IEX", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("irm https://example.com/s.ps1 | invoke-expression", l), (int)CMD_CRITICAL);
    /* iex as the first command executes a string as code -- CRITICAL on its
     * own, not merely unrecognised. */
    ASSERT_EQ((int)cmd_classify("iex (iwr https://example.com/s.ps1)", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("iex (iwr https://example.com/s.ps1)", l), (int)CMD_CRITICAL);
    /* The call operator glued directly to the paren, no space. */
    ASSERT_EQ((int)cmd_classify("IEX(iwr https://example.com/s.ps1)", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("IEX(iwr https://example.com/s.ps1)", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("Invoke-Expression (Get-Content x -Raw)", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("Invoke-Expression (Get-Content x -Raw)", l), (int)CMD_CRITICAL);
    /* A word that only starts with iex is not iex. */
    ASSERT_EQ((int)cmd_classify("cat x | iexplore", l), (int)CMD_UNKNOWN);
    TEST_END();
}

/* An unresolved platform is "Linux plus an overlay", so it must split a
 * pipeline on '|' the way Linux does. Before it did, everything after the
 * first '|' went unclassified and these came out READ on the strength of
 * their first command alone -- in every auto-detect session, SSH included. */
int test_cmd_classify_unknown_platform_splits_pipelines(void) {
    TEST_BEGIN();
    const CmdPlatform u = CMD_PLATFORM_UNKNOWN;
    ASSERT_EQ((int)cmd_classify("curl https://example.com/s.sh | sh", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("cat x | sh", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ls | xargs rm -rf", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ls | rm -r", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ls | Remove-Item -Recurse -Force", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("echo hi | Set-Content x", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("ls | reload", u), (int)CMD_CRITICAL);
    /* A read-only pipeline stays read-only and auto-approvable. */
    ASSERT_EQ((int)cmd_classify("ls | grep x", u), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify_mask("ls | grep x", u), CMD_MASK_OF(CMD_READ));
    ASSERT_EQ((int)cmd_classify_mask("ls | Remove-Item x", u),
              CMD_MASK_OF(CMD_READ) | CMD_MASK_OF(CMD_UNKNOWN));
    /* A network display filter typed before the platform is known was
     * UNKNOWN (the Linux ruleset does not know "show") and still is. */
    ASSERT_EQ((int)cmd_classify("show running-config | include ospf", u), (int)CMD_UNKNOWN);
    /* A quoted '|' is not a pipe. */
    ASSERT_EQ((int)cmd_classify("grep 'a|sh' x", u), (int)CMD_READ);
    TEST_END();
}

/* v1.2.9 classifier-holes fix: a lone '&' (PowerShell's call operator, and
 * the shell background operator) used to be swallowed whole -- next_token()
 * treats a bare '&' as end-of-input, so an unsplit segment starting with
 * '&' never reached a first token and classified READ. It is now a segment
 * separator on Linux and an unresolved platform, same as '|', ';' and
 * '&&', except when it's part of a redirect (">&", "&>", "2>&1", ...),
 * which never splits. */
int test_cmd_classify_ampersand_separator(void) {
    TEST_BEGIN();
    const CmdPlatform u = CMD_PLATFORM_UNKNOWN;
    const CmdPlatform l = CMD_PLATFORM_LINUX;

    /* A bare call-operator launch of an unrecognised path is at least
     * UNKNOWN, never READ. */
    ASSERT_TRUE((int)cmd_classify("& \"C:\\evil.exe\"", u) >= (int)CMD_UNKNOWN);
    ASSERT_TRUE((int)cmd_classify("& \"C:\\evil.exe\"", l) >= (int)CMD_UNKNOWN);

    /* "& Remove-Item ..." classifies exactly like "Remove-Item ..." alone
     * -- the call operator itself contributes nothing. */
    ASSERT_EQ((int)cmd_classify("& Remove-Item -Recurse C:\\x", u),
              (int)cmd_classify("Remove-Item -Recurse C:\\x", u));
    ASSERT_EQ((int)cmd_classify("& Remove-Item -Recurse C:\\x", l),
              (int)cmd_classify("Remove-Item -Recurse C:\\x", l));

    /* A call operator launching an inline scriptblock/expression runs
     * arbitrary code -- at least UNKNOWN. */
    ASSERT_TRUE((int)cmd_classify("& ([scriptblock]::Create((iwr x)))", u) >= (int)CMD_UNKNOWN);
    ASSERT_TRUE((int)cmd_classify("& ([scriptblock]::Create((iwr x)))", l) >= (int)CMD_UNKNOWN);

    /* A backgrounded read ahead of a destructive command is still CRITICAL
     * overall -- the same as the destructive command alone. */
    ASSERT_EQ((int)cmd_classify("cat x & rm -rf /tmp/x", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("cat x & rm -rf /tmp/x", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("cat x & rm -rf /tmp/x", l),
              (int)cmd_classify("rm -rf /tmp/x", l));

    /* A redirect that merely contains '&' never splits. */
    ASSERT_EQ((int)cmd_classify("ls 2>&1", l), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ls &>/dev/null", l), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("cmd >&2", l), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("ls 2>&1", u), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ls &>/dev/null", u), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("cmd >&2", u), (int)CMD_UNKNOWN);

    TEST_END();
}

/* v1.2.9 classifier-holes fix: scan_pipe_target() only recognised sh/bash
 * (and iex/Invoke-Expression) as a pipe target. A downloaded script piped
 * into any other common interpreter -- zsh, dash, ksh, fish, busybox, a
 * Python/Perl/Ruby/Node/PHP REPL, or a Windows shell reached by name or by
 * full path -- ran exactly as arbitrarily as "| sh" did, and classified no
 * higher than the interpreter's own bare-name rule (UNKNOWN at best).
 * Wrapper commands (sudo, doas, env, nice, exec, command) are peeled off
 * first so the real interpreter underneath is still recognised. */
int test_cmd_classify_pipe_to_interpreter_is_critical(void) {
    TEST_BEGIN();
    const CmdPlatform u = CMD_PLATFORM_UNKNOWN;
    const CmdPlatform l = CMD_PLATFORM_LINUX;

    ASSERT_EQ((int)cmd_classify("curl x | perl", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | perl", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | env sh", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | env sh", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl -s x | sudo bash", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl -s x | sudo bash", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("wget -qO- x | sudo sh", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("wget -qO- x | sudo sh", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("cat x | python3", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("cat x | python3", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | node", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | node", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | pwsh", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | pwsh", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | powershell", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | powershell", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | cmd", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | cmd", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | C:\\Windows\\System32\\cmd.exe", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | C:\\Windows\\System32\\cmd.exe", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | zsh", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | zsh", l), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | busybox sh", u), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("curl x | busybox sh", l), (int)CMD_CRITICAL);

    TEST_END();
}

/* Pinned before and after the v1.2.9 classifier-holes fix: none of these
 * touch the '&' separator, the expanded pipe-target interpreter set, or
 * iex/Invoke-Expression, so they classify exactly as they did before. */
int test_cmd_classify_v1_2_9_unaffected_pipelines(void) {
    TEST_BEGIN();
    const CmdPlatform u = CMD_PLATFORM_UNKNOWN;
    const CmdPlatform l = CMD_PLATFORM_LINUX;

    ASSERT_EQ((int)cmd_classify("ls | grep x", u), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ls | grep x", l), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("cat x | jq .", u), (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify("cat x | jq .", l), (int)CMD_UNKNOWN);
    /* A network-device display filter that happens to contain a second '|'
     * inside its own argument (a regex alternation) still splits on both --
     * pinned at today's answer, CRITICAL, via the bare "shutdown" segment
     * it produces. */
    ASSERT_EQ((int)cmd_classify("show run | include ^interface|shutdown", u),
              (int)CMD_CRITICAL);

    TEST_END();
}

/* ---- Quoting model: escaped quotes, unbalanced quoting, substitution ---- */

typedef struct {
    const char    *cmd;
    CmdSafetyLevel min_level;
} ClassifyMinCase;

/* Asserts cmd_classify(cmd) >= min on both LINUX and UNKNOWN; prints the
 * failing case so a table failure names its row. */
static int check_min_level_linuxish(const ClassifyMinCase *cases, size_t n)
{
    const CmdPlatform plats[2] = { CMD_PLATFORM_LINUX, CMD_PLATFORM_UNKNOWN };
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        for (int k = 0; k < 2; k++) {
            CmdSafetyLevel got = cmd_classify(cases[i].cmd, plats[k]);
            if (got < cases[i].min_level) {
                printf("  [%s] \"%s\": got %d, want >= %d\n",
                       plats[k] == CMD_PLATFORM_LINUX ? "linux" : "unknown",
                       cases[i].cmd, (int)got, (int)cases[i].min_level);
                bad = 1;
            }
        }
    }
    return bad;
}

/* Asserts cmd_classify(cmd) == level exactly on LINUX and UNKNOWN. */
static int check_exact_level_linuxish(const ClassifyMinCase *cases, size_t n)
{
    const CmdPlatform plats[2] = { CMD_PLATFORM_LINUX, CMD_PLATFORM_UNKNOWN };
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        for (int k = 0; k < 2; k++) {
            CmdSafetyLevel got = cmd_classify(cases[i].cmd, plats[k]);
            if (got != cases[i].min_level) {
                printf("  [%s] \"%s\": got %d, want %d\n",
                       plats[k] == CMD_PLATFORM_LINUX ? "linux" : "unknown",
                       cases[i].cmd, (int)got, (int)cases[i].min_level);
                bad = 1;
            }
        }
    }
    return bad;
}

/* An escaped quote is a literal character, not the start of a quoted span,
 * so whatever sits between two of them is still classified. Each wrapped
 * command must classify at least as severely as the command alone. */
int test_cmd_classify_escaped_quote_does_not_hide_segments(void) {
    TEST_BEGIN();
    static const char *inner[] = { "touch F", "rm F", "mv F G", "chmod 600 F" };
    static const char *wrap[] = {
        "echo \\'; %s; echo \\'",    /* POSIX backslash-escaped single quote */
        "echo \\\"; %s; echo \\\"",  /* POSIX backslash-escaped double quote */
        "echo `'; %s; echo `'",      /* PowerShell backtick-escaped single quote */
        "echo `\"; %s; echo `\"",    /* PowerShell backtick-escaped double quote */
        "echo \\' && %s && echo \\'",
        "echo \\' | %s",
    };
    const CmdPlatform plats[2] = { CMD_PLATFORM_LINUX, CMD_PLATFORM_UNKNOWN };
    for (size_t i = 0; i < sizeof inner / sizeof inner[0]; i++) {
        for (size_t w = 0; w < sizeof wrap / sizeof wrap[0]; w++) {
            char buf[128];
            snprintf(buf, sizeof buf, wrap[w], inner[i]);
            for (int k = 0; k < 2; k++) {
                CmdSafetyLevel alone = cmd_classify(inner[i], plats[k]);
                CmdSafetyLevel got = cmd_classify(buf, plats[k]);
                if (got < alone) {
                    printf("  \"%s\": got %d, \"%s\" alone is %d\n",
                           buf, (int)got, inner[i], (int)alone);
                    _tf_local_fail = 1;
                }
                unsigned mask = cmd_classify_mask(buf, plats[k]);
                if (!(mask & CMD_MASK_OF(alone))) {
                    printf("  \"%s\": mask 0x%x lacks level %d\n",
                           buf, mask, (int)alone);
                    _tf_local_fail = 1;
                }
            }
        }
    }
    /* The placeholders' own levels, pinned so the comparison above means
     * what it says. */
    ASSERT_EQ((int)cmd_classify("touch F", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("rm F", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("echo \\'; rm F; echo \\'", CMD_PLATFORM_LINUX),
              (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("echo \\'; touch F; echo \\'", CMD_PLATFORM_LINUX),
              (int)CMD_WRITE);
    TEST_END();
}

int test_cmd_classify_escaped_quote_redirect_is_write(void) {
    TEST_BEGIN();
    static const ClassifyMinCase exact[] = {
        { "echo it\\'s > F",   CMD_WRITE },
        { "echo it\\'s >> F",  CMD_WRITE },
        { "echo it`'s > F",    CMD_WRITE },
        /* An escaped '>' is literal to a POSIX shell but a redirect to
         * PowerShell (which does not treat a backslash as an escape). */
        { "echo a \\> F",      CMD_WRITE },
    };
    if (check_exact_level_linuxish(exact, sizeof exact / sizeof exact[0]))
        _tf_local_fail = 1;
    TEST_END();
}

int test_cmd_classify_ordinary_quoting_unchanged(void) {
    TEST_BEGIN();
    static const ClassifyMinCase exact[] = {
        { "echo 'a;b'",               CMD_READ },
        { "grep \"x|y\" f",           CMD_READ },
        { "echo \"it's\"",            CMD_READ },
        { "echo 'a && b' \"c | d\"",  CMD_READ },
        { "echo \"say \\\"hi\\\"\"",  CMD_READ },
        { "echo '$(id)'",             CMD_READ },
        { "echo 'a`b'",               CMD_READ },
        { "grep -r 'x > y' .",        CMD_READ },
        { "ls C:\\Users\\x",          CMD_READ },
        { "echo 'it''s'",             CMD_READ },
        { "cat f 2>&1 | grep x",      CMD_READ },
        { "ls > /dev/null 2>&1",      CMD_READ },
    };
    if (check_exact_level_linuxish(exact, sizeof exact / sizeof exact[0]))
        _tf_local_fail = 1;
    TEST_END();
}

int test_cmd_classify_unbalanced_quoting_is_unknown(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "echo 'abc",          CMD_UNKNOWN },
        { "echo \"abc",         CMD_UNKNOWN },
        { "echo abc\\",         CMD_UNKNOWN },
        { "echo abc`",          CMD_UNKNOWN },
        { "echo don\\'t",       CMD_UNKNOWN }, /* open quote to PowerShell */
        { "ls 'x; rm F",        CMD_UNKNOWN },
        { "echo a\\;b",         CMD_UNKNOWN }, /* ';' is live to PowerShell */
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    /* The mask of an ambiguous command carries UNKNOWN, so a mode that
     * permits only READ cannot auto-approve it. */
    ASSERT_TRUE((cmd_classify_mask("echo 'abc", CMD_PLATFORM_LINUX)
                 & CMD_MASK_OF(CMD_UNKNOWN)) != 0);
    TEST_END();
}

int test_cmd_classify_substitution_is_unknown(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "echo $(id)",               CMD_UNKNOWN },
        { "echo \"$(id)\"",           CMD_UNKNOWN },
        { "echo `id`",                CMD_UNKNOWN },
        { "diff <(ls a) <(ls b)",     CMD_UNKNOWN },
        { "cat $(ls)",                CMD_UNKNOWN },
        { "ls | grep $(whoami)",      CMD_UNKNOWN },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ---- Device-platform shell-metacharacter floor (device_shell_floor) ----
 *
 * On a network-device platform "|" is a display filter, not a shell pipe
 * (M1), so a device classifier's first-token check never even looks past
 * "show". device_shell_floor() is the minimum level under that: a
 * double-pipe "||" (never a display filter -- read the way a real Linux
 * shell would read it), a single "|" whose target is a shell/interpreter
 * or a filesystem-writing filter word, and an output redirect that has a
 * real target word after it. It never lowers a level the platform's own
 * classifier already found.
 *
 * The two tables below are pinned in different ways: "raised" is a
 * minimum the floor must reach regardless of what a future change to the
 * per-platform classifiers might also decide; "kept" is pinned to the
 * *exact* level today's classifier (before the floor existed) gives an
 * ordinary display filter or other unaffected command, computed by
 * running cmd_classify() against the pre-floor code -- so a regression
 * that makes the floor fire on a harmless filter is caught immediately,
 * not just "still at least as safe". */

typedef struct {
    CmdPlatform    platform;
    const char    *cmd;
    CmdSafetyLevel level;
} DeviceFloorCase;

static const char *device_floor_platform_name(CmdPlatform p)
{
    switch (p) {
    case CMD_PLATFORM_CISCO_IOS:  return "cisco_ios";
    case CMD_PLATFORM_CISCO_NXOS: return "cisco_nxos";
    case CMD_PLATFORM_CISCO_ASA:  return "cisco_asa";
    case CMD_PLATFORM_ARUBA_CX:   return "aruba_cx";
    case CMD_PLATFORM_ARUBA_OS:   return "aruba_os";
    case CMD_PLATFORM_JUNOS:      return "junos";
    case CMD_PLATFORM_PANOS:      return "panos";
    default:                      return "?";
    }
}

/* Asserts cmd_classify(cmd, platform) >= level for every row; prints the
 * platform and command of any row that falls short. */
static int check_device_floor_min(const DeviceFloorCase *cases, size_t n)
{
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        CmdSafetyLevel got = cmd_classify(cases[i].cmd, cases[i].platform);
        if (got < cases[i].level) {
            printf("  [%s (platform %d)] \"%s\": got %d, want >= %d\n",
                   device_floor_platform_name(cases[i].platform),
                   (int)cases[i].platform, cases[i].cmd,
                   (int)got, (int)cases[i].level);
            bad = 1;
        }
    }
    return bad;
}

/* Asserts cmd_classify(cmd, platform) == level exactly for every row. */
static int check_device_floor_exact(const DeviceFloorCase *cases, size_t n)
{
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        CmdSafetyLevel got = cmd_classify(cases[i].cmd, cases[i].platform);
        if (got != cases[i].level) {
            printf("  [%s (platform %d)] \"%s\": got %d, want %d\n",
                   device_floor_platform_name(cases[i].platform),
                   (int)cases[i].platform, cases[i].cmd,
                   (int)got, (int)cases[i].level);
            bad = 1;
        }
    }
    return bad;
}

/* Pipe target is a shell/interpreter (bare, with args, or behind sudo):
 * CRITICAL on every device platform, regardless of what "show version"
 * alone would classify as. */
int test_cmd_classify_device_floor_raises_pipe_to_interpreter(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_CISCO_IOS,  "show version | sh",           CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_NXOS, "show version | sh",           CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_ASA,  "show version | sh",           CMD_CRITICAL },
        { CMD_PLATFORM_JUNOS,      "show version | sh",           CMD_CRITICAL },
        { CMD_PLATFORM_PANOS,      "show version | sh",           CMD_CRITICAL },

        { CMD_PLATFORM_CISCO_IOS,  "show version | bash -c x",    CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_NXOS, "show version | bash -c x",    CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_ASA,  "show version | bash -c x",    CMD_CRITICAL },
        { CMD_PLATFORM_JUNOS,      "show version | bash -c x",    CMD_CRITICAL },
        { CMD_PLATFORM_PANOS,      "show version | bash -c x",    CMD_CRITICAL },

        { CMD_PLATFORM_CISCO_IOS,  "show version | python3",      CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_NXOS, "show version | python3",      CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_ASA,  "show version | python3",      CMD_CRITICAL },
        { CMD_PLATFORM_JUNOS,      "show version | python3",      CMD_CRITICAL },
        { CMD_PLATFORM_PANOS,      "show version | python3",      CMD_CRITICAL },

        { CMD_PLATFORM_CISCO_IOS,  "show run | sudo sh",          CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_NXOS, "show run | sudo sh",          CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_ASA,  "show run | sudo sh",          CMD_CRITICAL },
        { CMD_PLATFORM_JUNOS,      "show run | sudo sh",          CMD_CRITICAL },
        { CMD_PLATFORM_PANOS,      "show run | sudo sh",          CMD_CRITICAL },
    };
    if (check_device_floor_min(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* "||" is never a device display filter -- what runs after it is read the
 * way a real Linux shell would read it, and is never better than
 * UNKNOWN even when that reading is harmless. */
int test_cmd_classify_device_floor_raises_double_pipe(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_CISCO_IOS,  "show version || rm F",        CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_IOS,  "show version || cat F | sh",  CMD_CRITICAL },
        { CMD_PLATFORM_JUNOS,      "show version || cat F | sh",  CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_NXOS, "show version || rm F",        CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_ASA,  "show version || rm F",        CMD_CRITICAL },
        { CMD_PLATFORM_JUNOS,      "show version || rm F",        CMD_CRITICAL },
        { CMD_PLATFORM_PANOS,      "show version || rm F",        CMD_CRITICAL },

        { CMD_PLATFORM_CISCO_IOS,  "show version || touch F",     CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "show version || touch F",     CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA,  "show version || touch F",     CMD_WRITE },
        { CMD_PLATFORM_JUNOS,      "show version || touch F",     CMD_WRITE },
        { CMD_PLATFORM_PANOS,      "show version || touch F",     CMD_WRITE },

        { CMD_PLATFORM_CISCO_IOS,  "show version || frobnicate",  CMD_UNKNOWN },
        { CMD_PLATFORM_CISCO_NXOS, "show version || frobnicate",  CMD_UNKNOWN },
        { CMD_PLATFORM_CISCO_ASA,  "show version || frobnicate",  CMD_UNKNOWN },
        { CMD_PLATFORM_JUNOS,      "show version || frobnicate",  CMD_UNKNOWN },
        { CMD_PLATFORM_PANOS,      "show version || frobnicate",  CMD_UNKNOWN },
    };
    if (check_device_floor_min(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* An output redirect is a write only on a CLI with shell-style
 * redirection (NX-OS), only before the first display-filter '|', and only
 * when a real target word follows the '>'. Elsewhere '>' is not a
 * redirect, and the segment keeps the level the command alone has. */
int test_cmd_classify_device_floor_raises_redirect_with_target(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_CISCO_NXOS, "show run > F",                CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "show run >> F",               CMD_WRITE },
        /* NX-OS's own filesystem target spelling. */
        { CMD_PLATFORM_CISCO_NXOS, "show run > bootflash:x",      CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "show run >bootflash:x",       CMD_WRITE },
    };
    if (check_device_floor_min(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;

    static const CmdPlatform no_redirect[] = {
        CMD_PLATFORM_CISCO_IOS, CMD_PLATFORM_CISCO_ASA, CMD_PLATFORM_JUNOS,
        CMD_PLATFORM_PANOS, CMD_PLATFORM_ARUBA_CX, CMD_PLATFORM_ARUBA_OS,
    };
    for (size_t i = 0; i < sizeof no_redirect / sizeof no_redirect[0]; i++) {
        CmdSafetyLevel alone = cmd_classify("show run", no_redirect[i]);
        ASSERT_EQ((int)cmd_classify("show run > F", no_redirect[i]), (int)alone);
        ASSERT_EQ((int)cmd_classify("show run >> F", no_redirect[i]), (int)alone);
    }
    /* After the first display-filter '|', a '>' is part of the pattern,
     * even on NX-OS. */
    ASSERT_EQ((int)cmd_classify("show ip bgp | include *>i", CMD_PLATFORM_CISCO_NXOS),
              (int)cmd_classify("show ip bgp", CMD_PLATFORM_CISCO_NXOS));
    TEST_END();
}

/* A pipe target whose first word is a filesystem-writing filter (not a
 * shell) is at least WRITE: "redirect"/"append"/"tee" everywhere, "save"
 * on Junos where it captures filtered output to a file. */
int test_cmd_classify_device_floor_raises_write_filter_words(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_CISCO_IOS,  "show run | redirect flash:F", CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "show run | redirect flash:F", CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA,  "show run | redirect flash:F", CMD_WRITE },
        { CMD_PLATFORM_JUNOS,      "show run | redirect flash:F", CMD_WRITE },
        { CMD_PLATFORM_PANOS,      "show run | redirect flash:F", CMD_WRITE },

        { CMD_PLATFORM_CISCO_IOS,  "show run | append flash:F",   CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "show run | append flash:F",   CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA,  "show run | append flash:F",   CMD_WRITE },
        { CMD_PLATFORM_JUNOS,      "show run | append flash:F",   CMD_WRITE },
        { CMD_PLATFORM_PANOS,      "show run | append flash:F",   CMD_WRITE },

        { CMD_PLATFORM_CISCO_IOS,  "show run | tee flash:F",      CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "show run | tee flash:F",      CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA,  "show run | tee flash:F",      CMD_WRITE },
        { CMD_PLATFORM_JUNOS,      "show run | tee flash:F",      CMD_WRITE },
        { CMD_PLATFORM_PANOS,      "show run | tee flash:F",      CMD_WRITE },

        { CMD_PLATFORM_JUNOS,      "show configuration | save F", CMD_WRITE },
    };
    if (check_device_floor_min(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* enable secret / enable password set the privileged-mode secret -- a
 * config change -- on every platform where a bare "enable" reads as the
 * harmless mode change it is today (Cisco IOS and its NX-OS/ASA
 * delegates, and both Aruba families; confirmed against the pre-change
 * classifier that bare "enable" is CMD_READ on exactly these platforms). */
int test_cmd_classify_device_floor_enable_secret_is_write(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_CISCO_IOS,  "enable secret x",    CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "enable secret x",    CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA,  "enable secret x",    CMD_WRITE },
        { CMD_PLATFORM_ARUBA_CX,   "enable secret x",    CMD_WRITE },
        { CMD_PLATFORM_ARUBA_OS,   "enable secret x",    CMD_WRITE },

        { CMD_PLATFORM_CISCO_IOS,  "enable password x",  CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "enable password x",  CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA,  "enable password x",  CMD_WRITE },
        { CMD_PLATFORM_ARUBA_CX,   "enable password x",  CMD_WRITE },
        { CMD_PLATFORM_ARUBA_OS,   "enable password x",  CMD_WRITE },
    };
    if (check_device_floor_min(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* Pinned to the exact level today's classifier (pre-floor) gives an
 * ordinary display filter, a BGP best-path marker that ends in '>' with
 * nothing after it, a quoted '|' inside a filter argument, and bare
 * "enable" -- none of these may move even a single level, in either
 * direction, or the floor is misfiring on harmless input. Values for
 * "enable" differ by platform because they differed before this change:
 * CMD_READ where the platform recognises the bare verb, CMD_UNKNOWN
 * where it does not. */
int test_cmd_classify_device_floor_keeps_display_filters(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_CISCO_IOS,  "show run | include x",              CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show run | include x",              CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show run | include x",              CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show run | include x",              CMD_READ },
        { CMD_PLATFORM_PANOS,      "show run | include x",              CMD_READ },

        { CMD_PLATFORM_CISCO_IOS,  "show run | exclude x",              CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show run | exclude x",              CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show run | exclude x",              CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show run | exclude x",              CMD_READ },
        { CMD_PLATFORM_PANOS,      "show run | exclude x",              CMD_READ },

        { CMD_PLATFORM_CISCO_IOS,  "show run | begin x",                CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show run | begin x",                CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show run | begin x",                CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show run | begin x",                CMD_READ },
        { CMD_PLATFORM_PANOS,      "show run | begin x",                CMD_READ },

        { CMD_PLATFORM_CISCO_IOS,  "show run | section x",              CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show run | section x",              CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show run | section x",              CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show run | section x",              CMD_READ },
        { CMD_PLATFORM_PANOS,      "show run | section x",              CMD_READ },

        { CMD_PLATFORM_CISCO_IOS,  "show run | count x",                CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show run | count x",                CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show run | count x",                CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show run | count x",                CMD_READ },
        { CMD_PLATFORM_PANOS,      "show run | count x",                CMD_READ },

        /* BGP best-path marker: trailing '>' with nothing after it is not
         * a redirect. */
        { CMD_PLATFORM_CISCO_IOS,  "show ip bgp | include *>",          CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show ip bgp | include *>",          CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show ip bgp | include *>",          CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show ip bgp | include *>",          CMD_READ },
        { CMD_PLATFORM_PANOS,      "show ip bgp | include *>",          CMD_READ },

        { CMD_PLATFORM_CISCO_IOS,  "show interfaces | i up",            CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show interfaces | i up",            CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show interfaces | i up",            CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show interfaces | i up",            CMD_READ },
        { CMD_PLATFORM_PANOS,      "show interfaces | i up",            CMD_READ },

        { CMD_PLATFORM_JUNOS,      "show configuration | display set",  CMD_READ },

        { CMD_PLATFORM_CISCO_IOS,  "show route | match x",              CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show route | match x",              CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show route | match x",              CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show route | match x",              CMD_READ },
        { CMD_PLATFORM_PANOS,      "show route | match x",              CMD_READ },

        { CMD_PLATFORM_CISCO_IOS,  "show interfaces terse | except down", CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show interfaces terse | except down", CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show interfaces terse | except down", CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show interfaces terse | except down", CMD_READ },
        { CMD_PLATFORM_PANOS,      "show interfaces terse | except down", CMD_READ },

        { CMD_PLATFORM_PANOS,      "show system info | match x",        CMD_READ },

        /* A '|' inside single quotes is not active -- not a second pipe
         * target. */
        { CMD_PLATFORM_CISCO_IOS,  "show run | include 'a|b'",          CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "show run | include 'a|b'",          CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "show run | include 'a|b'",          CMD_READ },
        { CMD_PLATFORM_JUNOS,      "show run | include 'a|b'",          CMD_READ },
        { CMD_PLATFORM_PANOS,      "show run | include 'a|b'",          CMD_READ },

        /* Bare "enable": unaffected either way -- READ where recognised,
         * UNKNOWN where it was already unrecognised before this change. */
        { CMD_PLATFORM_CISCO_IOS,  "enable",                            CMD_READ },
        { CMD_PLATFORM_CISCO_NXOS, "enable",                            CMD_READ },
        { CMD_PLATFORM_CISCO_ASA,  "enable",                            CMD_READ },
        { CMD_PLATFORM_JUNOS,      "enable",                            CMD_UNKNOWN },
        { CMD_PLATFORM_PANOS,      "enable",                            CMD_UNKNOWN },
    };
    if (check_device_floor_exact(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ---- Hardening: a flag that writes, sends data, changes state or runs
 * another program must lift a command that would otherwise be READ purely
 * because it matched an allow-list entry. Table-driven, checked on both
 * CMD_PLATFORM_LINUX and CMD_PLATFORM_UNKNOWN via check_min_level_linuxish
 * (>= min, not necessarily exact -- some rows may legitimately classify
 * higher, e.g. via a redirect or sudo elevation elsewhere in the
 * classifier). Placeholder file names ("F", "G") and a placeholder URL
 * ("https://x") stand in for real arguments throughout. */
int test_cmd_classify_hardened_flags_raise_level(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        /* sed -i in any spelling: WRITE */
        { "sed -i s/a/b/ F",                    CMD_WRITE },
        { "sed -i.bak s/a/b/ F",                CMD_WRITE },
        { "sed -i'' s/a/b/ F",                  CMD_WRITE },
        { "sed --in-place s/a/b/ F",            CMD_WRITE },
        { "sed --in-place=.bak s/a/b/ F",       CMD_WRITE },
        { "sed -Ei s/a/b/ F",                   CMD_WRITE },
        { "sed -ni s/a/b/p F",                  CMD_WRITE },
        { "sed -si s/a/b/ F",                   CMD_WRITE },
        /* sed script writes (w/W command, s///w flag): WRITE */
        { "sed 'w G' F",                        CMD_WRITE },
        { "sed 'W G' F",                        CMD_WRITE },
        { "sed 's/a/b/w G' F",                  CMD_WRITE },
        /* sed script runs a shell command (e command, s///e flag): UNKNOWN */
        { "sed 's/a/b/e' F",                    CMD_UNKNOWN },
        { "sed 'e' F",                          CMD_UNKNOWN },
        /* sed -f/--file: script from a file, contents unknown: UNKNOWN */
        /* write/exec commands behind an address, or later in the script */
        { "sed '1w G' F",                       CMD_WRITE },
        { "sed '/x/w G' F",                     CMD_WRITE },
        { "sed '$W G' F",                       CMD_WRITE },
        { "sed -n '1,5p;w G' F",                CMD_WRITE },
        { "sed -e p -e 'w G' F",                CMD_WRITE },
        { "sed --expression='w G' F",           CMD_WRITE },
        { "sed -n -e '1{w G' -e '}' F",         CMD_WRITE },
        { "sed 's/a b/c d/w G' F",              CMD_WRITE },
        { "sed 's|a|b|gw G' F",                 CMD_WRITE },
        { "sed '1e id' F",                      CMD_UNKNOWN },
        { "sed '/x/e' F",                       CMD_UNKNOWN },
        { "sed 's/a/b/ge' F",                   CMD_UNKNOWN },
        { "sed -f script.sed F",                CMD_UNKNOWN },
        { "sed --file=script.sed F",            CMD_UNKNOWN },

        /* perl -i in any spelling: WRITE */
        { "perl -i -e 's/a/b/' F",              CMD_WRITE },
        { "perl -pi -e 's/a/b/' F",             CMD_WRITE },
        { "perl -i.bak -e 's/a/b/' F",          CMD_WRITE },
        { "perl -pie s/a/b/ F",                 CMD_WRITE },
        { "perl -0pi -e 's/a/b/' F",            CMD_WRITE },
        /* perl is never READ, in-place or not: at least UNKNOWN */
        { "perl -e 'print 1'",                  CMD_UNKNOWN },
        { "perl -ne 'print' F",                 CMD_UNKNOWN },
        { "perl -lne 'print' F",                CMD_UNKNOWN },
        { "perl F",                             CMD_UNKNOWN },
        /* ruby -e / python(3) -c: at least UNKNOWN */
        { "ruby -e 'puts 1'",                   CMD_UNKNOWN },
        { "python -c 'print(1)'",               CMD_UNKNOWN },
        { "python3 -c 'print(1)'",              CMD_UNKNOWN },

        /* find -exec/-execdir/-ok/-okdir: classify the executed command */
        { "find . -exec mv {} G \\;",           CMD_WRITE },
        { "find . -exec shred {} \\;",          CMD_CRITICAL },
        { "find . -exec cat {} \\;",            CMD_UNKNOWN },
        { "find . -exec rm {} +",               CMD_CRITICAL },
        { "find . -ok rm {} \\;",               CMD_CRITICAL },
        { "find . -execdir shred {} \\;",       CMD_CRITICAL },
        /* the scan continues past the -exec terminator */
        { "find . -exec cat {} \\; -exec rm {} \\;", CMD_CRITICAL },
        { "find . -exec cat {} ';' -exec rm {} ';'", CMD_CRITICAL },
        { "find . -exec cat {} \\; -delete",    CMD_CRITICAL },
        { "find . -exec cat {} + -fprint G",    CMD_WRITE },
        { "find . -exec /bin/rm -f {} +",       CMD_CRITICAL },
        { "find . -exec",                       CMD_UNKNOWN },
        /* find -fprint/-fprint0/-fprintf/-fls: WRITE */
        { "find . -fprint G",                   CMD_WRITE },
        { "find . -fprint0 G",                  CMD_WRITE },
        { "find . -fprintf G '%p\\n'",          CMD_WRITE },
        { "find . -fls G",                      CMD_WRITE },
        /* find primary outside the known-safe allow-list: at least UNKNOWN */
        { "find . -something-unknown",          CMD_UNKNOWN },

        /* curl: writes, sends data, or leaks something -> WRITE */
        { "curl -o F https://x",                CMD_WRITE },
        { "curl -oF https://x",                 CMD_WRITE },
        { "curl -sSLo F https://x",             CMD_WRITE },
        { "curl --output=F https://x",          CMD_WRITE },
        { "curl --output F https://x",          CMD_WRITE },
        { "curl -O https://x",                  CMD_WRITE },
        { "curl --remote-name https://x",       CMD_WRITE },
        { "curl --remote-name-all https://x",   CMD_WRITE },
        { "curl -J -O https://x",               CMD_WRITE },
        { "curl --output-dir G -O https://x",   CMD_WRITE },
        { "curl --create-dirs -o G/F https://x", CMD_WRITE },
        { "curl -T F https://x",                CMD_WRITE },
        { "curl --upload-file F https://x",     CMD_WRITE },
        { "curl -d data https://x",             CMD_WRITE },
        { "curl --data data https://x",         CMD_WRITE },
        { "curl --data-raw data https://x",     CMD_WRITE },
        { "curl --json '{}' https://x",         CMD_WRITE },
        { "curl -F field=val https://x",        CMD_WRITE },
        { "curl --form field=val https://x",    CMD_WRITE },
        { "curl -X POST https://x",             CMD_WRITE },
        { "curl --request POST https://x",      CMD_WRITE },
        { "curl -c G https://x",                CMD_WRITE },
        { "curl --cookie-jar G https://x",      CMD_WRITE },
        { "curl -D F https://x",                CMD_WRITE },
        { "curl --dump-header F https://x",     CMD_WRITE },
        { "curl --trace F https://x",           CMD_WRITE },
        { "curl --libcurl F https://x",         CMD_WRITE },
        { "curl --etag-save F https://x",       CMD_WRITE },
        /* curl -K/--config: UNKNOWN */
        { "curl -K F https://x",                CMD_UNKNOWN },
        { "curl --config F https://x",          CMD_UNKNOWN },
        /* curl flag outside the known-safe set: at least UNKNOWN */
        { "curl --weird-unknown-flag https://x", CMD_UNKNOWN },

        /* sort -o/--output (incl. combined and attached): WRITE */
        { "sort -o G F",                        CMD_WRITE },
        { "sort -oG F",                         CMD_WRITE },
        { "sort -uo G F",                       CMD_WRITE },
        { "sort --output=G F",                  CMD_WRITE },
        { "sort --output G F",                  CMD_WRITE },
        /* sort --compress-program: UNKNOWN */
        { "sort --compress-program gzip F",     CMD_UNKNOWN },
        /* uniq: a second file argument is the output file: WRITE */
        { "uniq F G",                           CMD_WRITE },

        /* git branch: creating/renaming/deleting is WRITE */
        { "git branch -d G",                    CMD_WRITE },
        { "git branch -D G",                    CMD_WRITE },
        { "git branch --delete G",              CMD_WRITE },
        { "git branch G",                       CMD_WRITE },
        { "git branch -u origin/main",          CMD_WRITE },
        /* git log/diff/show --output: WRITE; --ext-diff: UNKNOWN */
        { "git log --output=F",                 CMD_WRITE },
        { "git diff --output=F",                CMD_WRITE },
        { "git show --output=F",                CMD_WRITE },
        { "git log --ext-diff",                 CMD_UNKNOWN },
        /* git global options that can run an arbitrary program: UNKNOWN */
        { "git -c core.pager=cat log",          CMD_UNKNOWN },
        { "git --exec-path=/tmp log",           CMD_UNKNOWN },
        { "git --git-dir=/tmp log",             CMD_UNKNOWN },
        { "git --work-tree=/tmp log",           CMD_UNKNOWN },

        /* ip: flush/delete/del -> CRITICAL; add/append/replace/change/set/
         * prepend/save/restore -> WRITE, generalised to every object */
        { "ip route flush cache",               CMD_CRITICAL },
        { "ip route del default",               CMD_CRITICAL },
        { "ip addr add 10.0.0.1/24 dev eth0",   CMD_WRITE },
        { "ip link set eth0 up",                CMD_CRITICAL },
        { "ip rule add from 10.0.0.0/24 table 1", CMD_WRITE },
        { "ip route replace default via 10.0.0.1", CMD_WRITE },
        { "ip netns delete myns",               CMD_CRITICAL },
        { "ip netns add myns",                  CMD_WRITE },
        /* ip netns exec: classify the executed command, at least UNKNOWN */
        { "ip netns exec myns rm F",            CMD_CRITICAL },
        { "ip netns exec myns ls",              CMD_UNKNOWN },
        /* ip -batch/-b/-force: UNKNOWN */
        { "ip -batch F",                        CMD_UNKNOWN },
        { "ip -b F",                            CMD_UNKNOWN },
        { "ip -force route del default",        CMD_CRITICAL },

        /* history: state/file-changing flags -> WRITE */
        { "history -c",                         CMD_WRITE },
        { "history -w",                         CMD_WRITE },
        { "history -d 5",                       CMD_WRITE },
        { "history -a",                         CMD_WRITE },
        { "history -r",                         CMD_WRITE },
        { "history -n",                         CMD_WRITE },
        { "history -s echo hi",                 CMD_WRITE },
        /* history -p: expansion only, not a pure query either -> UNKNOWN */
        { "history -p",                         CMD_UNKNOWN },

        /* terraform plan: can run arbitrary provider code -> UNKNOWN */
        { "terraform plan",                     CMD_UNKNOWN },

        /* man: pager/browser flag runs another program -> UNKNOWN */
        { "man -P less ls",                     CMD_UNKNOWN },
        { "man --pager=less ls",                CMD_UNKNOWN },
        { "man -H ls",                          CMD_UNKNOWN },
        { "man --html ls",                      CMD_UNKNOWN },
        /* rg --pre: filters through an external command -> UNKNOWN */
        { "rg --pre cat x",                     CMD_UNKNOWN },
        { "rg --pre=cat x",                     CMD_UNKNOWN },
        /* less: "+!cmd" runs a shell command -> UNKNOWN; log-file -> WRITE */
        { "less +!sh",                          CMD_UNKNOWN },
        { "less '+!sh'",                        CMD_UNKNOWN },
        { "less -o G",                          CMD_WRITE },
        { "less -O G",                          CMD_WRITE },
        { "less --log-file=G",                  CMD_WRITE },
        { "less --LOG-FILE=G",                  CMD_WRITE },
        { "less -oG F",                         CMD_WRITE },
        { "less -OG F",                         CMD_WRITE },
        { "less '+|sh' F",                      CMD_UNKNOWN },
        { "less +G!sh F",                       CMD_UNKNOWN },
        { "history -cw",                        CMD_WRITE },
        { "history -d5",                        CMD_WRITE },

        /* Wrappers: the result is never lower than the wrapped command */
        { "timeout 5 rm F",                     CMD_CRITICAL },
        { "timeout -k 5 -s TERM 5 rm F",        CMD_CRITICAL },
        { "env rm F",                           CMD_CRITICAL },
        { "env -i rm F",                        CMD_CRITICAL },
        { "env -u PATH rm F",                   CMD_CRITICAL },
        { "env VAR=x rm F",                     CMD_CRITICAL },
        { "nohup touch F",                      CMD_WRITE },
        { "nice -n 10 rm F",                    CMD_CRITICAL },
        /* exec is no longer a pass-through wrapper (round 3): it replaces
         * the current shell process rather than running the command
         * alongside it, so "exec CMD" is at least UNKNOWN, not whatever CMD
         * alone would be -- see test_cmd_classify_exec_not_pass_through_wrapper. */
        { "exec rm F",                          CMD_UNKNOWN },
        { "exec -a NAME rm F",                  CMD_UNKNOWN },
        { "command -p rm F",                    CMD_CRITICAL },
        { "time rm F",                          CMD_CRITICAL },
        { "stdbuf -oL rm F",                    CMD_CRITICAL },
        { "ionice -c2 rm F",                    CMD_CRITICAL },
        { "setsid rm F",                        CMD_CRITICAL },
        /* env -S: re-parses a whole string as a command line -> UNKNOWN */
        { "env -S 'rm F' G",                    CMD_UNKNOWN },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ---- Hardening: commands the classifier must still recognise as READ --
 * an explicit regression table alongside the "raised" one above, so a
 * change that widened the hardening too far shows up here. Checked on both
 * CMD_PLATFORM_LINUX and CMD_PLATFORM_UNKNOWN via check_exact_level_linuxish
 * (== READ exactly). */
int test_cmd_classify_hardened_flags_keep_read(void) {
    TEST_BEGIN();
    static const ClassifyMinCase exact[] = {
        { "ls -la",                                    CMD_READ },
        { "cat f",                                      CMD_READ },
        { "grep -r x .",                                 CMD_READ },
        { "find . -name '*.c'",                          CMD_READ },
        { "find /var/log -type f -mtime +7 -print",       CMD_READ },
        { "find . -iname '*.c' -o -perm -4000",          CMD_READ },
        { "find . -newerXY reference",                    CMD_READ },
        { "curl -s https://x",                            CMD_READ },
        { "curl -sSL https://x",                          CMD_READ },
        { "curl -s -o /dev/null -w '%{http_code}' https://x", CMD_READ },
        { "curl -I https://x",                            CMD_READ },
        { "curl -o /dev/null https://x",                  CMD_READ },
        { "curl -o - https://x",                          CMD_READ },
        { "curl -D - https://x",                          CMD_READ },
        { "curl -D /dev/null https://x",                  CMD_READ },
        { "curl -X GET https://x",                        CMD_READ },
        { "curl -X HEAD https://x",                       CMD_READ },
        { "sed -n 1,5p f",                                CMD_READ },
        { "sed 's/a/b/' f",                               CMD_READ },
        { "sed -e 's/a/b/g' f",                           CMD_READ },
        /* file operands are not script: names starting with w or e */
        { "sed -n 1p error.log",                          CMD_READ },
        { "sed -n '/x/p' www.log",                        CMD_READ },
        { "sed 's/w/e/' f",                               CMD_READ },
        { "sed -n '$p' f",                                CMD_READ },
        { "sed '/^#/d;s/a b/c/' f",                       CMD_READ },
        { "sed -e p -e 's/x/y/g' wfile efile",            CMD_READ },
        { "less +G F",                                    CMD_READ },
        { "less -N F",                                    CMD_READ },
        { "find . -name x -o -name y -print",             CMD_READ },
        { "sort f",                                       CMD_READ },
        { "sort -k1 f",                                   CMD_READ },
        { "uniq -c in",                                   CMD_READ },
        { "df -h",                                        CMD_READ },
        { "git log --oneline",                            CMD_READ },
        { "git branch",                                   CMD_READ },
        { "git branch -a",                                CMD_READ },
        { "git branch --show-current",                    CMD_READ },
        { "git branch --sort=-committerdate",             CMD_READ },
        { "git -C dir log --oneline",                     CMD_READ },
        { "ip addr show",                                 CMD_READ },
        { "ip a",                                         CMD_READ },
        { "ip route",                                     CMD_READ },
        { "ip -br link",                                  CMD_READ },
        { "ip -s link show eth0",                         CMD_READ },
        { "ip route get 1.1.1.1",                         CMD_READ },
        { "ip neigh",                                     CMD_READ },
        { "history",                                      CMD_READ },
        { "history 5",                                    CMD_READ },
        { "rg --pre-glob '*.gz' x",                       CMD_READ },
        { "man ls",                                       CMD_READ },
        { "command -v ls",                                CMD_READ },
        { "command -V ls",                                CMD_READ },
        { "nice -n 10 ls",                                CMD_READ },
        { "env",                                          CMD_READ },
        { "printenv",                                     CMD_READ },
    };
    if (check_exact_level_linuxish(exact, sizeof exact / sizeof exact[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ---- Hardening: sudo'd forms of the newly-hardened commands never
 * classify below WRITE, same as every other sudo'd command. */
int test_cmd_classify_hardened_flags_sudo_never_below_write(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "sudo sed -n p f",                    CMD_WRITE },
        { "sudo curl -s https://x",              CMD_WRITE },
        { "sudo find . -name x",                 CMD_WRITE },
        { "sudo sort f",                         CMD_WRITE },
        { "sudo uniq f",                         CMD_WRITE },
        { "sudo history",                        CMD_WRITE },
        { "sudo man ls",                         CMD_WRITE },
        { "sudo rg x f",                         CMD_WRITE },
        { "sudo less f",                         CMD_WRITE },
        { "sudo git branch",                     CMD_WRITE },
        { "sudo ip a",                           CMD_WRITE },
        { "sudo terraform plan",                 CMD_WRITE },
        { "sudo nice -n 10 ls",                  CMD_WRITE },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ===================================================================
 * Review round 2: quote-aware word splitting, flag allow-lists, ip verbs,
 * curl output/upload rules, the PowerShell reading, sed brackets, the
 * device display filters and glued redirect targets. Placeholders: F, G
 * for files, x for anything else, https://x for a URL.
 * =================================================================== */

/* Asserts cmd_classify(cmd, platform) >= min for one platform. */
static int check_min_on(const ClassifyMinCase *cases, size_t n, CmdPlatform plat)
{
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        CmdSafetyLevel got = cmd_classify(cases[i].cmd, plat);
        if (got < cases[i].min_level) {
            printf("  [platform %d] \"%s\": got %d, want >= %d\n",
                   (int)plat, cases[i].cmd, (int)got, (int)cases[i].min_level);
            bad = 1;
        }
    }
    return bad;
}

/* A quoted or escaped separator ( ; | < > ) is part of a word, so a flag
 * after it is still seen: env, curl, sort, git and ip, with single quotes,
 * double quotes and a backslash escape. */
int test_cmd_classify_quoted_separator_before_flag(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "env -u 'x;y' rm F",                      CMD_CRITICAL },
        { "env -u 'x|y' rm F",                      CMD_CRITICAL },
        { "env -u 'x<y' rm F",                      CMD_CRITICAL },
        { "env -u 'x>y' rm F",                      CMD_CRITICAL },
        { "env -u \"x;y\" rm F",                    CMD_CRITICAL },
        { "env LANG='x;y' rm F",                    CMD_CRITICAL },

        { "curl -H 'x;y' -o F https://x",           CMD_WRITE },
        { "curl -H 'x|y' -o F https://x",           CMD_WRITE },
        { "curl -H 'x<y' -o F https://x",           CMD_WRITE },
        { "curl -H 'x>y' -o F https://x",           CMD_WRITE },
        { "curl -A \"x;y\" -T F https://x",         CMD_WRITE },

        { "sort -t ';' -o F G",                     CMD_WRITE },
        { "sort -t '|' -o F G",                     CMD_WRITE },
        { "sort -t '<' -o F G",                     CMD_WRITE },
        { "sort -t '>' -o F G",                     CMD_WRITE },
        { "sort -t \";\" -o F G",                   CMD_WRITE },
        { "sort -t \\; -o F G",                     CMD_WRITE },

        { "git log --grep='x;y' --output=F",        CMD_WRITE },
        { "git log --grep='x|y' --output=F",        CMD_WRITE },
        { "git log --grep='x<y' --output=F",        CMD_WRITE },
        { "git log --grep='x>y' --output=F",        CMD_WRITE },
        { "git -C 'x;y' log --output=F",            CMD_WRITE },
        { "git branch --list 'x;y' -D x",           CMD_WRITE },

        { "ip -n 'x;y' route flush all",            CMD_CRITICAL },
        { "ip -n 'x|y' link set dev x down",        CMD_CRITICAL },
        { "ip -n 'x<y' addr del x dev x",           CMD_CRITICAL },
        { "ip -n 'x>y' addr flush dev x",           CMD_CRITICAL },

        /* An input redirection or an fd redirect before the flag no longer
         * ends the scan. */
        { "sort <F -o G",                           CMD_WRITE },
        { "sort 2>/dev/null -o G F",                CMD_WRITE },
        { "git log >/dev/null --output=F",          CMD_WRITE },
        /* A quote hiding the leading '-' of a flag. */
        { "sort '-o' F G",                          CMD_UNKNOWN },
        { "curl \"-o\" F https://x",                CMD_UNKNOWN },
        { "ip '-b' F",                              CMD_UNKNOWN },
        /* One word to a POSIX shell, two to PowerShell: both readings. */
        { "sort x\\ -o F",                          CMD_WRITE },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* Allow-lists: a flag outside the list is at least UNKNOWN, an
 * abbreviation of a flag known to write is WRITE, an attached value on an
 * unlisted letter is not mistaken for harmless letters. */
int test_cmd_classify_allow_list_unlisted_flags(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "sort --o=F G",                  CMD_WRITE },
        { "sort --outp F G",               CMD_WRITE },
        { "sort -oF G",                    CMD_WRITE },
        { "sort -no F G",                  CMD_WRITE },
        { "sort --compress-prog=x G",      CMD_UNKNOWN },
        { "sort --compress-program=x G",   CMD_UNKNOWN },
        { "sort --rev G",                  CMD_UNKNOWN },
        { "uniq F G",                      CMD_WRITE },
        { "uniq -c F G",                   CMD_WRITE },
        { "uniq --gro F",                  CMD_UNKNOWN },
        { "man --pag=x ls",                CMD_UNKNOWN },
        { "man -Px ls",                    CMD_UNKNOWN },
        { "man -P x ls",                   CMD_UNKNOWN },
        { "man -Hx ls",                    CMD_UNKNOWN },
        { "man -l F",                      CMD_UNKNOWN },
        { "less --log-f=F G",              CMD_WRITE },
        { "less -oF G",                    CMD_WRITE },
        { "less -O F G",                   CMD_WRITE },
        { "less -k F G",                   CMD_UNKNOWN },
        { "less +v G",                     CMD_UNKNOWN },
        { "less '+!x' G",                  CMD_UNKNOWN },
        { "less '+|x' G",                  CMD_UNKNOWN },
        { "less +sF G",                    CMD_UNKNOWN },
        { "rg --pre=x x",                  CMD_UNKNOWN },
        { "rg --pre x x",                  CMD_UNKNOWN },
        { "rg --pr x x",                   CMD_UNKNOWN },
        { "history -c",                    CMD_WRITE },
        { "history -w F",                  CMD_WRITE },
        { "history -p x",                  CMD_UNKNOWN },
        { "git branch --unset-ups",        CMD_WRITE },
        { "git branch --edit-desc",        CMD_WRITE },
        { "git branch x",                  CMD_WRITE },
        { "git branch -f x",               CMD_WRITE },
        { "git log --outp=F",              CMD_WRITE },
        { "git log --o=F",                 CMD_WRITE },
        { "git diff --output F",           CMD_WRITE },
        { "git log --ext-diff",            CMD_UNKNOWN },
        { "git log --onel",                CMD_UNKNOWN },
        { "git status --frob",             CMD_UNKNOWN },
        { "git -c x=y log",                CMD_UNKNOWN },
        { "git --exec-path=x log",         CMD_UNKNOWN },
        { "git --git-dir=x log",           CMD_UNKNOWN },
        { "git remote add x https://x",    CMD_WRITE },
        { "git push",                      CMD_WRITE },
        { "time -o F ls",                  CMD_WRITE },
        { "time -oF ls",                   CMD_WRITE },
        { "/usr/bin/time --out=F ls",      CMD_WRITE },
        { "/usr/bin/time --output F ls",   CMD_WRITE },
        { "time -a -o F ls",               CMD_WRITE },
        { "time --frob ls",                CMD_UNKNOWN },
        { "time rm F",                     CMD_CRITICAL },
        { "nohup ls",                      CMD_WRITE },
        { "nohup rm F",                    CMD_CRITICAL },
        { "env LD_PRELOAD=x ls",           CMD_UNKNOWN },
        { "env PAGER=x man ls",            CMD_UNKNOWN },
        { "env --frob ls",                 CMD_UNKNOWN },
        { "env -S 'rm F'",                 CMD_CRITICAL },
        { "env --split-string='rm F'",     CMD_CRITICAL },
        { "timeout --frob 5 ls",           CMD_UNKNOWN },
        { "timeout 5 rm F",                CMD_CRITICAL },
        { "nice --frob ls",                CMD_UNKNOWN },
        { "ionice -p 1",                   CMD_WRITE },
        { "ionice -c3 rm F",               CMD_CRITICAL },
        { "stdbuf -oL rm F",               CMD_CRITICAL },
        { "setsid -f rm F",                CMD_CRITICAL },
        { "tree -o F",                     CMD_WRITE },
        { "sar -o F 1 1",                  CMD_WRITE },
        { "ss -K dst x",                   CMD_WRITE },
        { "ss --kill",                     CMD_WRITE },
        { "ss -D F",                       CMD_WRITE },
        { "arp -d x",                      CMD_WRITE },
        { "arp -s x y",                    CMD_WRITE },
        { "file -C -m F",                  CMD_WRITE },
        { "dmidecode --dump-bin F",        CMD_WRITE },
        { "sensors -s",                    CMD_WRITE },
        { "iptables-save -f F",            CMD_WRITE },
        { "iptables-save -M x",            CMD_UNKNOWN },
        { "lastlog -C -u x",               CMD_WRITE },
        { "journalctl --rotate",           CMD_WRITE },
        { "journalctl --vac=1",            CMD_WRITE },
        { "journalctl --vacuum-size=1M",   CMD_WRITE },
        { "journalctl -n --rotate",        CMD_WRITE },
        { "journalctl --frob",             CMD_UNKNOWN },
        { "route add default gw x",        CMD_WRITE },
        { "route -n del default",          CMD_CRITICAL },
        { "route -n add default gw x",     CMD_WRITE },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* Ordinary read-only usage stays READ under the allow-lists, on both
 * LINUX and UNKNOWN. */
int test_cmd_classify_allow_list_keeps_read(void) {
    TEST_BEGIN();
    static const ClassifyMinCase exact[] = {
        { "ls -la",                                        CMD_READ },
        { "cat f",                                         CMD_READ },
        { "grep -r x .",                                   CMD_READ },
        { "find . -name '*.c'",                            CMD_READ },
        { "curl -s https://x",                             CMD_READ },
        { "sed -n 1,5p f",                                 CMD_READ },
        { "git log --oneline",                             CMD_READ },
        { "git -C dir log --oneline",                      CMD_READ },
        { "ip a",                                          CMD_READ },
        { "ip -s link show eth0",                          CMD_READ },
        { "ip -j addr show",                               CMD_READ },
        { "sort f",                                        CMD_READ },
        { "sort -n f",                                     CMD_READ },
        { "uniq -c f",                                     CMD_READ },
        { "df -h",                                         CMD_READ },
        { "man ls",                                        CMD_READ },
        { "less f",                                        CMD_READ },
        { "rg x",                                          CMD_READ },
        /* further common read forms */
        { "git status",                                    CMD_READ },
        { "git status -sb",                                CMD_READ },
        { "git status --porcelain=v1",                     CMD_READ },
        { "git diff --stat",                               CMD_READ },
        { "git diff --cached",                             CMD_READ },
        { "git diff HEAD~1 -- f",                          CMD_READ },
        { "git log -n 5 --format='%h %s'",                 CMD_READ },
        { "git log -5 --graph --decorate --all",           CMD_READ },
        { "git log --since='2 weeks ago' --author=x",      CMD_READ },
        { "git log -p -S x -- f",                          CMD_READ },
        { "git show HEAD --stat",                          CMD_READ },
        { "git --no-pager log -3",                         CMD_READ },
        { "git branch -vv",                                CMD_READ },
        { "git branch -a --contains HEAD",                 CMD_READ },
        { "git branch --list 'f*'",                        CMD_READ },
        { "git branch --merged main",                      CMD_READ },
        { "git rev-parse --show-toplevel",                 CMD_READ },
        { "git rev-parse --abbrev-ref HEAD",               CMD_READ },
        { "git remote -v",                                 CMD_READ },
        { "git ls-files",                                  CMD_READ },
        { "git blame -L 1,5 f",                            CMD_READ },
        { "ip -br a",                                      CMD_READ },
        { "ip -c link",                                    CMD_READ },
        { "ip -4 addr show dev eth0",                      CMD_READ },
        { "ip route get 1.1.1.1",                          CMD_READ },
        { "ip route list table all",                       CMD_READ },
        { "ip -details link show",                         CMD_READ },
        { "ip neigh show",                                 CMD_READ },
        { "ip link help",                                  CMD_READ },
        { "route -n",                                      CMD_READ },
        { "sort -t, -k2 -n f",                             CMD_READ },
        { "sort -u f",                                     CMD_READ },
        { "sort -rh f",                                    CMD_READ },
        { "uniq -d f",                                     CMD_READ },
        { "less -R f",                                     CMD_READ },
        { "less +G f",                                     CMD_READ },
        { "less -N +/x f",                                 CMD_READ },
        { "man -k x",                                      CMD_READ },
        { "man 5 passwd",                                  CMD_READ },
        { "rg -n --hidden -g '*.c' x",                     CMD_READ },
        { "rg -i -C 3 x src",                              CMD_READ },
        { "journalctl -u x -n 50 --no-pager",              CMD_READ },
        { "journalctl -f",                                 CMD_READ },
        { "journalctl -b -1 -p err",                       CMD_READ },
        { "journalctl --since today",                      CMD_READ },
        { "dmesg -T",                                      CMD_READ },
        { "date +%s",                                      CMD_READ },
        { "hostname -I",                                   CMD_READ },
        { "env",                                           CMD_READ },
        { "env LANG=C sort f",                             CMD_READ },
        { "time ls",                                       CMD_READ },
        { "nice -n 10 ls",                                 CMD_READ },
        { "timeout 5 ls",                                  CMD_READ },
        { "curl -sS -o /dev/null -w '%{http_code}' https://x", CMD_READ },
        { "curl -s -D - -o /dev/null https://x",           CMD_READ },
        { "curl --silent --location https://x",            CMD_READ },
        { "curl -H 'Accept: x' https://x",                 CMD_READ },
        { "curl -b 'a=b' https://x",                       CMD_READ },
        { "ss -tlnp",                                      CMD_READ },
        { "tree -L 2",                                     CMD_READ },
        { "history 20",                                    CMD_READ },
        { "ls 2>/dev/null",                                CMD_READ },
        { "ls >/dev/null 2>&1",                            CMD_READ },
        { "ls &>/dev/null",                                CMD_READ },
        { "cut -d' ' -f1 f",                               CMD_READ },
        { "sed 's/[/]/x/' f",                              CMD_READ },
        /* An escaped blank splits differently in the two readings; both
         * are READ, so the command is. */
        { "ls My\\ Documents",                             CMD_READ },
        { "sed -n '/[[:space:]]x/p' f",                    CMD_READ },
    };
    if (check_exact_level_linuxish(exact, sizeof exact / sizeof exact[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ip: READ only with no verb or exactly show/list/lst/get/help. Any other
 * verb, abbreviations included, is WRITE; flush/delete forms and link set
 * are CRITICAL. Global options consume their values. */
int test_cmd_classify_ip_verbs(void) {
    TEST_BEGIN();
    static const ClassifyMinCase exact[] = {
        { "ip a d 10.0.0.1/24 dev x",            CMD_CRITICAL },
        { "ip a f dev x",                        CMD_CRITICAL },
        { "ip r fl all",                         CMD_CRITICAL },
        { "ip addr del x dev x",                 CMD_CRITICAL },
        { "ip route delete default",             CMD_CRITICAL },
        { "ip neigh flush all",                  CMD_CRITICAL },
        { "ip link set dev x down",              CMD_CRITICAL },
        { "ip l s x down",                       CMD_CRITICAL },
        { "ip -f inet a flush",                  CMD_CRITICAL },
        { "ip -family inet addr del x dev x",    CMD_CRITICAL },
        { "ip -n x link set x down",             CMD_CRITICAL },
        { "ip -netns x addr flush dev x",        CMD_CRITICAL },
        { "ip -rc 1 route del x",                CMD_CRITICAL },
        { "ip -l 1 addr flush dev x",            CMD_CRITICAL },
        { "ip -loops 1 addr flush dev x",        CMD_CRITICAL },
        { "ip --json route flush all",           CMD_CRITICAL },
        { "ip a s",                              CMD_WRITE },
        { "ip a add x dev x",                    CMD_WRITE },
        { "ip route replace x via x",            CMD_WRITE },
        { "ip addr change x dev x",              CMD_WRITE },
        { "ip route save",                       CMD_WRITE },
        { "ip -b F",                             CMD_UNKNOWN },
        { "ip -batch F",                         CMD_UNKNOWN },
        { "ip -force a",                         CMD_UNKNOWN },
        { "ip -frob a",                          CMD_UNKNOWN },
        { "ip firewall x",                       CMD_UNKNOWN },
        { "ip a",                                CMD_READ },
        { "ip",                                  CMD_READ },
        { "ip addr show",                        CMD_READ },
        { "ip addr lst",                         CMD_READ },
        { "ip route list",                       CMD_READ },
        { "ip -color=always a",                  CMD_READ },
        { "ip xfrm state",                       CMD_READ },
        { "ip xfrm policy list",                 CMD_READ },
        { "ip xfrm state flush",                 CMD_CRITICAL },
        { "ip -all netns exec rm F",             CMD_CRITICAL },
        { "ip netns exec x rm F",                CMD_CRITICAL },
    };
    if (check_exact_level_linuxish(exact, sizeof exact / sizeof exact[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* curl: -w writes with %output{}, reads its format from a file with @;
 * -H/-b read local files; -K reads a config; the -o /dev/null exception
 * needs every other output, upload and config flag to be absent. */
int test_cmd_classify_curl_output_rules(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "curl -w '%output{F}' https://x",               CMD_WRITE },
        { "curl --write-out '%output{F}x' https://x",     CMD_WRITE },
        { "curl -w @F https://x",                         CMD_UNKNOWN },
        { "curl -H @F https://x",                         CMD_WRITE },
        { "curl --header @F https://x",                   CMD_WRITE },
        { "curl -H'@F' https://x",                        CMD_WRITE },
        { "curl -b F https://x",                          CMD_WRITE },
        { "curl -b @F https://x",                         CMD_WRITE },
        { "curl --cookie F https://x",                    CMD_WRITE },
        { "curl -K F https://x",                          CMD_UNKNOWN },
        { "curl --config F https://x",                    CMD_UNKNOWN },
        { "curl -o /dev/null -O https://x",               CMD_WRITE },
        { "curl -o /dev/null -o F https://x https://x",   CMD_WRITE },
        { "curl -o /dev/null -o /dev/null https://x https://x", CMD_WRITE },
        { "curl -o - -D F https://x",                     CMD_WRITE },
        { "curl -o /dev/null -T F https://x",             CMD_WRITE },
        { "curl -o /dev/null --next https://x",           CMD_WRITE },
        { "curl -o /dev/null -: https://x",               CMD_WRITE },
        { "curl -o /dev/null -K F https://x",             CMD_WRITE },
        { "curl -o /dev/null -d @F https://x",            CMD_WRITE },
        { "curl -o /dev/null -w @F https://x",            CMD_WRITE },
        { "curl -o /dev/null -w '%output{F}' https://x",  CMD_WRITE },
        { "curl -o '/dev/null' --upload-file F https://x", CMD_WRITE },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* PowerShell reading on an unresolved platform: typographic quotes are
 * string delimiters there, and an unquoted "(" / "@(" / "$(" runs the
 * command inside it. */
int test_cmd_classify_powershell_quotes_and_subexpressions(void) {
    TEST_BEGIN();
    const CmdPlatform u = CMD_PLATFORM_UNKNOWN;
    static const ClassifyMinCase unknown_cases[] = {
        /* \xE2\x80\x98 / \xE2\x80\x99 are U+2018 / U+2019, \xE2\x80\x9C /
         * \xE2\x80\x9D are U+201C / U+201D. */
        { "echo \xE2\x80\x98x\xE2\x80\x99",               CMD_UNKNOWN },
        { "ls \xE2\x80\x9C" "F\xE2\x80\x9D",              CMD_UNKNOWN },
        /* A typographic quote closes an ASCII one in PowerShell, which
         * leaves the rm active there. */
        { "echo '\xE2\x80\x98; rm F'",                    CMD_CRITICAL },
        { "echo \"\xE2\x80\x9D; rm F\"",                  CMD_CRITICAL },
        { "echo (rm F)",                                  CMD_CRITICAL },
        { "echo @(rm F)",                                 CMD_CRITICAL },
        { "echo x(ls)",                                   CMD_UNKNOWN },
        { "echo $(rm F)",                                 CMD_CRITICAL },
    };
    if (check_min_on(unknown_cases, sizeof unknown_cases / sizeof unknown_cases[0], u))
        _tf_local_fail = 1;
    /* On a Linux session the typographic quotes are ordinary characters. */
    ASSERT_EQ((int)cmd_classify("echo \xE2\x80\x98x\xE2\x80\x99", CMD_PLATFORM_LINUX),
              (int)CMD_READ);
    /* Substitution is classified inside on every platform. */
    ASSERT_EQ((int)cmd_classify("echo $(rm F)", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("ls `rm F`", CMD_PLATFORM_LINUX), (int)CMD_CRITICAL);
    ASSERT_EQ((int)cmd_classify("echo $(ls)", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);
    /* Quoted parentheses are text. */
    ASSERT_EQ((int)cmd_classify("echo '(rm F)'", u), (int)CMD_READ);
    TEST_END();
}

/* sed: a bracket expression may hide the delimiter; both readings are
 * taken. Anything left unparsed is UNKNOWN. */
int test_cmd_classify_sed_brackets_and_unparsed(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "sed '/[/]/w F' G",            CMD_WRITE },
        { "sed 's/[/]/x/w F' G",         CMD_WRITE },
        { "sed '/[]/]/w F' G",           CMD_WRITE },
        { "sed -n '/[[:alpha:]/]/w F' G", CMD_WRITE },
        { "sed -n '/x' G",               CMD_UNKNOWN },
        { "sed 's/a/b' G",               CMD_UNKNOWN },
        { "sed 'y/abc/xyz' G",           CMD_UNKNOWN },
        { "sed '/[x/w F' G",             CMD_UNKNOWN },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* Device display filters: keywords match case-insensitively and by unique
 * prefix, as the CLI does; ordinary BGP filters keep their level. */
int test_cmd_classify_device_filter_abbreviations(void) {
    TEST_BEGIN();
    static const DeviceFloorCase raised[] = {
        { CMD_PLATFORM_CISCO_IOS,  "show run | red flash:x",          CMD_WRITE },
        { CMD_PLATFORM_CISCO_IOS,  "show run | REDIRECT flash:x",     CMD_WRITE },
        { CMD_PLATFORM_CISCO_IOS,  "show run | appe flash:x",         CMD_WRITE },
        { CMD_PLATFORM_CISCO_IOS,  "show run | t flash:x",            CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA,  "show run | Red flash:x",          CMD_WRITE },
        { CMD_PLATFORM_JUNOS,      "show configuration | sa F",       CMD_WRITE },
        { CMD_PLATFORM_JUNOS,      "show configuration | SAVE F",     CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS, "show run | em x",                 CMD_WRITE },
        { CMD_PLATFORM_CISCO_IOS,  "show run | i x || rm F",          CMD_CRITICAL },
    };
    if (check_device_floor_min(raised, sizeof raised / sizeof raised[0]))
        _tf_local_fail = 1;

    static const struct { CmdPlatform plat; const char *cmd; const char *base; } kept[] = {
        { CMD_PLATFORM_CISCO_IOS,  "show run | s bgp",               "show run" },
        { CMD_PLATFORM_CISCO_IOS,  "show ip bgp | i *>i",            "show ip bgp" },
        { CMD_PLATFORM_CISCO_IOS,  "show ip bgp | include *> 10.0",  "show ip bgp" },
        { CMD_PLATFORM_CISCO_IOS,  "show ip bgp | i a||b",           "show ip bgp" },
        { CMD_PLATFORM_CISCO_NXOS, "show ip bgp | i *>i",            "show ip bgp" },
        { CMD_PLATFORM_CISCO_NXOS, "show ip bgp | include *> 10.0",  "show ip bgp" },
        { CMD_PLATFORM_CISCO_NXOS, "show ip bgp | i a||b",           "show ip bgp" },
        { CMD_PLATFORM_CISCO_ASA,  "show ip bgp | i *>i",            "show ip bgp" },
        { CMD_PLATFORM_JUNOS,      "show interfaces | t",            "show interfaces" },
        { CMD_PLATFORM_JUNOS,      "show route | m x",               "show route" },
    };
    for (size_t i = 0; i < sizeof kept / sizeof kept[0]; i++) {
        CmdSafetyLevel got = cmd_classify(kept[i].cmd, kept[i].plat);
        CmdSafetyLevel want = cmd_classify(kept[i].base, kept[i].plat);
        if (got != want) {
            printf("  [platform %d] \"%s\": got %d, want %d\n",
                   (int)kept[i].plat, kept[i].cmd, (int)got, (int)want);
            _tf_local_fail = 1;
        }
    }
    ASSERT_EQ((int)cmd_classify("show ip bgp | i *>i", CMD_PLATFORM_CISCO_IOS), (int)CMD_READ);
    TEST_END();
}

/* A redirect target glued to its operator is a file of that name, not the
 * safe target it starts with. */
int test_cmd_classify_glued_redirect_targets(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "ls >&2F",          CMD_WRITE },
        { "ls >/dev/nullF",   CMD_WRITE },
        { "ls 2>/dev/nullx",  CMD_WRITE },
        { "ls >&1x",          CMD_WRITE },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    static const ClassifyMinCase exact[] = {
        { "ls 2>&1",          CMD_READ },
        { "ls >&2",           CMD_READ },
        { "ls >/dev/null",    CMD_READ },
        { "ls > /dev/null",   CMD_READ },
        { "ls 2>&-",          CMD_READ },
    };
    if (check_exact_level_linuxish(exact, sizeof exact / sizeof exact[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ---- Round 3: expansion floor for allow-listed READ commands ---- */

/* An active (unquoted, or double-quoted where it still expands) shell
 * expansion in an argument of find/sort/git log/curl/sed/less/uniq can
 * produce a flag or run something the per-command checks never see, so it
 * makes the segment at least UNKNOWN, even though the raw word looks like
 * a harmless operand (does not itself start with '-'). Each command's word
 * is deliberately shaped so the pre-fix classifier saw nothing to object
 * to: a brace/dollar-quote token that does not start with '-', or a plain
 * "$@"/"${IFS}" operand next to (not glued to) a genuine flag. */
int test_cmd_classify_expansion_floor_at_least_unknown(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        /* Brace expansion with a comma. */
        { "find . {-delete,-print}",       CMD_UNKNOWN },
        { "sort {-o,-r} F",                CMD_UNKNOWN },
        { "git log {--oneline,--output=F}", CMD_UNKNOWN },
        { "curl {-o,-s} http://x/F",       CMD_UNKNOWN },
        { "sed 's/a/b/' {-i,-n}",          CMD_UNKNOWN },
        { "less {-o,-O} F",                CMD_UNKNOWN },
        { "uniq {-c,-d}",                  CMD_UNKNOWN },
        /* $'...' (ANSI-C quoting) can also produce a flag. */
        { "find . $'-delete'",             CMD_UNKNOWN },
        { "sort $'-o' F",                  CMD_UNKNOWN },
        { "git log $'-oneline'",           CMD_UNKNOWN },
        { "curl $'-o' F http://x",         CMD_UNKNOWN },
        { "sed 's/a/b/' $'-i'",            CMD_UNKNOWN },
        { "less $'-o' F",                  CMD_UNKNOWN },
        { "uniq $'-w'",                    CMD_UNKNOWN },
        /* $@/${IFS} ahead of, or in place of, a flag word. */
        { "find . $@ -true",               CMD_UNKNOWN },
        { "sort ${IFS} F",                 CMD_UNKNOWN },
        { "git log $@",                    CMD_UNKNOWN },
        { "curl $@ http://x",              CMD_UNKNOWN },
        { "sed 's/a/b/' $@",               CMD_UNKNOWN },
        { "less $@ F",                     CMD_UNKNOWN },
        { "uniq $@",                       CMD_UNKNOWN },
        /* A plain, non-exempt variable in an ordinary operand position. */
        { "sort $X F",                     CMD_UNKNOWN },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* The exemption (HOME/PWD/USER/LOGNAME/HOSTNAME, bare or "${NAME}", and a
 * leading '~') holds regardless of whether the command is one the
 * expansion floor checks at all. */
int test_cmd_classify_expansion_floor_exempt_stays_read(void) {
    TEST_BEGIN();
    ASSERT_EQ((int)cmd_classify("ls $HOME", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("cat ~/F", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("ls \"$PWD\"", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("echo $USER", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("grep x ~/F", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("sort $HOME/F", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("git log ${HOME}", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* ---- Round 3: pidstat -e, dig -f, tree -R ---- */

int test_cmd_classify_pidstat_dig_tree_flag_specs(void) {
    TEST_BEGIN();
    static const ClassifyMinCase unknown_cases[] = {
        { "pidstat -e sh -c x",   CMD_UNKNOWN },   /* -e starts and monitors a program */
        { "dig -f F example.com", CMD_UNKNOWN },   /* -f reads a local file and sends it */
        { "tree -R",              CMD_UNKNOWN },   /* not a reviewed-safe tree flag */
    };
    if (check_min_level_linuxish(unknown_cases, sizeof unknown_cases / sizeof unknown_cases[0]))
        _tf_local_fail = 1;

    /* Everyday forms of all three stay READ. */
    ASSERT_EQ((int)cmd_classify("pidstat 1 5", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("pidstat -u -p 1234", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dig +short example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dig -t MX example.com", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dig -x 8.8.8.8", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("tree -a -L 2", CMD_PLATFORM_LINUX), (int)CMD_READ);
    TEST_END();
}

/* ---- Round 3: device floor '&' split and '|&' pipe scan ---- */

/* Device platforms don't split on a lone '&' at the top level the way
 * Linux and an unresolved platform do, so the floor has to catch it (and a
 * "|&" pipe) itself -- the same way it already catches "||" and "|". */
int test_cmd_classify_device_floor_ampersand_and_pipeamp(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_CISCO_IOS,  "show run & rm -rf F", CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_NXOS, "show run & rm -rf F", CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_IOS,  "show run |& sh",      CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_NXOS, "show run |& sh",      CMD_CRITICAL },
    };
    if (check_device_floor_min(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;

    /* A trailing bare '&' with nothing after it is plain backgrounding, not
     * a shell escape: no floor bump over "show run" alone. */
    ASSERT_EQ((int)cmd_classify("show run &", CMD_PLATFORM_CISCO_IOS),
              (int)cmd_classify("show run", CMD_PLATFORM_CISCO_IOS));
    TEST_END();
}

/* VyOS's operational mode is bash underneath and HP Comware also writes a
 * file on a bare '>' before any display filter, like NX-OS. */
int test_cmd_classify_device_floor_redirect_vyos_comware(void) {
    TEST_BEGIN();
    static const DeviceFloorCase cases[] = {
        { CMD_PLATFORM_VYOS,       "show configuration > F",             CMD_WRITE },
        { CMD_PLATFORM_VYOS,       "show configuration >> F",            CMD_WRITE },
        { CMD_PLATFORM_HP_COMWARE, "display current-configuration > F",  CMD_WRITE },
        { CMD_PLATFORM_HP_COMWARE, "display current-configuration >> F", CMD_WRITE },
    };
    if (check_device_floor_min(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;

    /* Ordinary display and BGP filters are unaffected: '>' after the first
     * '|' is part of the pattern, not a redirect, on these two either. */
    ASSERT_EQ((int)cmd_classify("show configuration | match >x", CMD_PLATFORM_VYOS),
              (int)cmd_classify("show configuration", CMD_PLATFORM_VYOS));
    ASSERT_EQ((int)cmd_classify("display ip bgp | i *>i", CMD_PLATFORM_HP_COMWARE),
              (int)cmd_classify("display ip bgp", CMD_PLATFORM_HP_COMWARE));
    TEST_END();
}

/* ---- Round 3: exec is no longer a pass-through wrapper ---- */

/* "exec CMD" replaces the current shell process rather than running CMD
 * alongside it, so it is at least UNKNOWN, not whatever CMD alone would be
 * ("exec ls" used to classify exactly like "ls": READ). */
int test_cmd_classify_exec_not_pass_through_wrapper(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "exec ls",      CMD_UNKNOWN },
        { "exec -a x ls", CMD_UNKNOWN },
    };
    if (check_min_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ---- Round 3: "|&" is a pipe ---- */

int test_cmd_classify_pipe_amp_is_pipe(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "cat F |& sh",  CMD_CRITICAL },
        { "cat F|& sh",   CMD_CRITICAL },
    };
    if (check_exact_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ---- Round 3: "ip netns exec" / "ip vrf exec" by prefix ---- */

int test_cmd_classify_ip_netns_vrf_exec_prefix(void) {
    TEST_BEGIN();
    static const ClassifyMinCase cases[] = {
        { "ip netns exec ns1 ls",       CMD_WRITE },
        { "ip vrf exec blue ls",        CMD_WRITE },
        { "ip netn e ns1 ls",           CMD_WRITE },
        { "ip n e ns1 ls",              CMD_WRITE },
        { "ip vrf e blue ls",           CMD_WRITE },
        /* The inner command's own level wins when it is worse than WRITE --
         * an abbreviated object/verb must still be classified recursively,
         * not just fall through to the flat WRITE any other unrecognised ip
         * verb gets regardless of what follows it. */
        { "ip netns exec ns1 rm -rf F", CMD_CRITICAL },
        { "ip n e ns1 rm -rf F",        CMD_CRITICAL },
        { "ip vrf e blue rm -rf F",     CMD_CRITICAL },
    };
    if (check_exact_level_linuxish(cases, sizeof cases / sizeof cases[0]))
        _tf_local_fail = 1;
    TEST_END();
}

/* ===== Contradicted sessions: cmd_classify_session() (H1, 2026-09-24) =====
 * CMD_PLATFORM_UNKNOWN is not the strictest ruleset -- it covers Linux, but
 * every device ruleset rates some of its own commands above it. A session
 * whose platform host output has contradicted keeps that platform and is
 * judged under the worse of the two. */

typedef struct {
    CmdPlatform    plat;
    const char    *cmd;
    CmdSafetyLevel expect;   /* the worse of the two, as measured */
} SessionCase;

int test_cmd_classify_session_contradicted_device_examples(void) {
    TEST_BEGIN();
    static const SessionCase cases[] = {
        /* Stricter under the device ruleset than under UNKNOWN. */
        { CMD_PLATFORM_CISCO_IOS, "copy running-config startup-config", CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_IOS, "clear ip bgp *",                     CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_IOS, "format flash:",                      CMD_CRITICAL },
        { CMD_PLATFORM_JUNOS,     "file delete /var/tmp/old.tgz",       CMD_CRITICAL },
        { CMD_PLATFORM_VYOS,      "reset ip bgp all",                   CMD_CRITICAL },
        { CMD_PLATFORM_MIKROTIK,  "reset",                              CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_IOS, "hostname core1",                     CMD_WRITE },
        { CMD_PLATFORM_CISCO_NXOS,"hostname core1",                     CMD_WRITE },
        { CMD_PLATFORM_CISCO_ASA, "hostname fw1",                       CMD_WRITE },
        { CMD_PLATFORM_CISCO_IOS, "ip route 0.0.0.0 0.0.0.0 10.0.0.1",  CMD_WRITE },
        /* Stricter under UNKNOWN than under the resolved ruleset. */
        { CMD_PLATFORM_LINUX,     "reload",                             CMD_CRITICAL },
        { CMD_PLATFORM_LINUX,     "commit",                             CMD_CRITICAL },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const SessionCase *c = &cases[i];
        CmdSafetyLevel own = cmd_classify(c->cmd, c->plat);
        CmdSafetyLevel unk = cmd_classify(c->cmd, CMD_PLATFORM_UNKNOWN);
        CmdSafetyLevel got = cmd_classify_session(c->cmd, c->plat, 1);
        ASSERT_TRUE(got >= own);
        ASSERT_TRUE(got >= unk);
        ASSERT_EQ((int)got, (int)c->expect);

        unsigned mask = cmd_classify_mask_session(c->cmd, c->plat, 1);
        unsigned own_m = cmd_classify_mask(c->cmd, c->plat);
        unsigned unk_m = cmd_classify_mask(c->cmd, CMD_PLATFORM_UNKNOWN);
        ASSERT_EQ(mask & own_m, own_m);
        ASSERT_EQ(mask & unk_m, unk_m);
        ASSERT_TRUE((mask & CMD_MASK_OF(got)) != 0);
    }
    TEST_END();
}

/* Not contradicted: exactly the resolved platform's ruleset, nothing more --
 * and an unresolved session is just UNKNOWN whichever way the flag is set. */
int test_cmd_classify_session_not_contradicted_is_plain(void) {
    TEST_BEGIN();
    static const SessionCase cases[] = {
        { CMD_PLATFORM_LINUX,     "reload",                       CMD_UNKNOWN },
        { CMD_PLATFORM_JUNOS,     "file delete /var/tmp/old.tgz", CMD_CRITICAL },
        { CMD_PLATFORM_CISCO_IOS, "show version",                 CMD_READ },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const SessionCase *c = &cases[i];
        ASSERT_EQ((int)cmd_classify_session(c->cmd, c->plat, 0),
                  (int)cmd_classify(c->cmd, c->plat));
        ASSERT_EQ(cmd_classify_mask_session(c->cmd, c->plat, 0),
                  cmd_classify_mask(c->cmd, c->plat));
    }
    ASSERT_EQ((int)cmd_classify_session("reload", CMD_PLATFORM_LINUX, 0),
              (int)CMD_UNKNOWN);
    ASSERT_EQ((int)cmd_classify_session("reload", CMD_PLATFORM_UNKNOWN, 1),
              (int)cmd_classify("reload", CMD_PLATFORM_UNKNOWN));
    ASSERT_EQ(cmd_classify_mask_session("ls; reload", CMD_PLATFORM_UNKNOWN, 1),
              cmd_classify_mask("ls; reload", CMD_PLATFORM_UNKNOWN));
    ASSERT_EQ((int)cmd_classify_session(NULL, CMD_PLATFORM_CISCO_IOS, 1),
              (int)CMD_READ);
    TEST_END();
}

/* Property over a corpus drawn from this file's per-platform cases, every
 * platform: contradicted is never looser than the platform alone nor than
 * UNKNOWN alone, its mask covers both, and it is the max of the two. */
int test_cmd_classify_session_worse_of_two_property(void) {
    TEST_BEGIN();
    static const char *corpus[] = {
        "ls -la", "cat /etc/passwd", "rm -rf /tmp/x", "reboot", "reload",
        "shutdown -h now", "systemctl restart nginx", "apt-get install vim",
        "echo hi > /etc/motd", "find / -name x -delete", "sudo -i", "commit",
        "rollback", "undo", "purge", "boot", "restore", "factory-reset",
        "show running-config", "show version", "show ip bgp summary",
        "show interfaces", "copy running-config startup-config",
        "copy tftp: flash:", "write memory", "write erase",
        "erase startup-config", "clear ip bgp *", "clear counters",
        "format flash:", "delete flash:old.bin", "configure terminal",
        "hostname core1", "ip route 0.0.0.0 0.0.0.0 10.0.0.1",
        "interface GigabitEthernet0/1", "no shutdown", "shutdown",
        "reload in 5", "debug all", "terminal length 0",
        "file delete /var/tmp/old.tgz", "file show /var/log/messages",
        "request system reboot", "request system zeroize",
        "request support info", "set system host-name r1", "delete interfaces",
        "run show route", "show configuration", "commit confirmed 5",
        "reset ip bgp all", "reset conntrack", "reset",
        "/system reset-configuration", "/system reboot", "/ip address print",
        "/interface print", "/export", "execute reboot",
        "execute factoryreset", "get system status", "config system global",
        "diagnose sys top", "save", "display current-configuration",
        "reset saved-configuration", "system-view", "sysname sw1",
        "debug", "test", "ping 10.0.0.1", "traceroute 10.0.0.1",
        "ls | xargs rm", "cat x; reload", "show run | include hostname",
        "", "   ", "zzz-not-a-command --flag",
    };
    for (int pi = 0; pi <= (int)CMD_PLATFORM_UNKNOWN; pi++) {
        CmdPlatform plat = (CmdPlatform)pi;
        for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); i++) {
            const char *cmd = corpus[i];
            CmdSafetyLevel own = cmd_classify(cmd, plat);
            CmdSafetyLevel unk = cmd_classify(cmd, CMD_PLATFORM_UNKNOWN);
            CmdSafetyLevel got = cmd_classify_session(cmd, plat, 1);
            ASSERT_TRUE(got >= own);
            ASSERT_TRUE(got >= unk);
            ASSERT_EQ((int)got, (int)(own > unk ? own : unk));

            unsigned own_m = cmd_classify_mask(cmd, plat);
            unsigned unk_m = cmd_classify_mask(cmd, CMD_PLATFORM_UNKNOWN);
            ASSERT_EQ(cmd_classify_mask_session(cmd, plat, 1), own_m | unk_m);
        }
    }
    TEST_END();
}
