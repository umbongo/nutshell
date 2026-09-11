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
    ASSERT_EQ((int)cmd_classify("terraform plan", CMD_PLATFORM_LINUX), (int)CMD_READ);
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
    ASSERT_EQ((int)cmd_classify("date +%Y-%m-%d", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);

    ASSERT_EQ((int)cmd_classify("hostname", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("hostname newname", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);

    ASSERT_EQ((int)cmd_classify("dmesg", CMD_PLATFORM_LINUX), (int)CMD_READ);
    ASSERT_EQ((int)cmd_classify("dmesg -C", CMD_PLATFORM_LINUX), (int)CMD_WRITE);
    ASSERT_EQ((int)cmd_classify("dmesg -T", CMD_PLATFORM_LINUX), (int)CMD_UNKNOWN);

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
