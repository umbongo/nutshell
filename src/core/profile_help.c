#include "profile_help.h"
#include <stddef.h>

#define PH(id, label, text) { id, #id, label, text }

static const ProfileHelp TABLE[PH_COUNT] = {
    PH(PH_KIND, "Type",
       "How this session connects: over SSH, as a shell on this PC, or over "
       "Telnet. The type decides which of the other fields are used. Telnet "
       "sends everything, passwords included, without encryption."),
    PH(PH_NAME, "Name",
       "The name shown in the list of saved sessions. If you leave it empty, "
       "the list shows the host name instead; a local shell session has no "
       "host, so give it a name."),
    PH(PH_HOST, "Host",
       "The address of the server, as a name or an IP address. It cannot be "
       "empty: a session without a host is not saved."),
    PH(PH_PORT, "Port",
       "The port the server listens on. SSH usually uses 22 and Telnet 23. If "
       "you leave it empty, or enter something that is not a number from 1 to "
       "65535, the usual port for the type is used."),
    PH(PH_USER, "User",
       "The account to log in as. SSH needs one: Nutshell does not ask for "
       "it, so with this empty the server refuses the login. For Telnet, "
       "Nutshell types it at the login prompt; leave it empty to type it "
       "yourself."),
    PH(PH_AUTH, "Auth",
       "How you prove who you are: with a password, or with an SSH key file."),
    PH(PH_PLATFORM, "Platform",
       "The kind of system at the other end, such as a Linux server or a "
       "network switch. The AI assistant uses it to judge which commands are "
       "safe to run. Auto-detect works it out from the session."),
    PH(PH_PASSWORD, "Password",
       "The password, or with an SSH key the key's passphrase. It is saved "
       "encrypted for your Windows account. If you leave it empty, Nutshell "
       "asks when it is needed. With Telnet and no user either, you type both "
       "at the server's prompts."),
    PH(PH_KEY, "Key",
       "The path of your private key file, used when Auth is SSH Key. If you "
       "leave it empty, the key login cannot work, so give a path or choose "
       "Password."),
    PH(PH_BROWSE, "Browse",
       "Choose the private key file in a file dialog instead of typing its "
       "path."),
    PH(PH_SHELL, "Shell",
       "Automatic (the first choice) picks one installed on this PC: "
       "PowerShell 7, Windows PowerShell, Git Bash, MSYS2 bash or cmd, in "
       "that order."),
    PH(PH_AI_NOTES, "AI Notes",
       "Notes about this server for the AI assistant, such as what it is used "
       "for or what to avoid, up to about 400 words. They are sent to your AI "
       "provider with your questions. If you leave them empty, the assistant "
       "gets no extra notes."),
};

const ProfileHelp *profile_help_get(int id)
{
    if (id < 0 || id >= (int)PH_COUNT) return NULL;
    return &TABLE[id];
}
