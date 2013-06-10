/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>

#include "ui.h"
#include "cutils/properties.h"
#include "install.h"
#include "common.h"
#include "adb_install.h"
extern "C" {
#include "minadbd/adb.h"
#include "twinstall.h"
#include "data.h"
int TWinstall_zip(const char* path, int* wipe_cache);
}

static RecoveryUI* ui = NULL;

static void
set_usb_driver(bool enabled) {
    int fd = open("/sys/class/android_usb/android0/enable", O_WRONLY);
    if (fd < 0) {
/* These error messages show when built in older Android branches (e.g. Gingerbread)
   It's not a critical error so we're disabling the error messages.
        ui->Print("failed to open driver control: %s\n", strerror(errno));
*/
		LOGI("failed to open driver control: %s\n", strerror(errno));
        return;
    }
    if (write(fd, enabled ? "1" : "0", 1) < 0) {
/*
        ui->Print("failed to set driver control: %s\n", strerror(errno));
*/
		LOGI("failed to set driver control: %s\n", strerror(errno));
    }
    if (close(fd) < 0) {
/*
        ui->Print("failed to close driver control: %s\n", strerror(errno));
*/
		LOGI("failed to close driver control: %s\n", strerror(errno));
    }
}

static void
stop_adbd() {
    property_set("ctl.stop", "adbd");
    set_usb_driver(false);
}


static void
maybe_restart_adbd() {
    char value[PROPERTY_VALUE_MAX+1];
    int len = property_get("ro.debuggable", value, NULL);
    if (len == 1 && value[0] == '1') {
        ui->Print("Restarting adbd...\n");
        set_usb_driver(true);
        property_set("ctl.start", "adbd");
    }
}

int
apply_from_adb(RecoveryUI* ui_, int* wipe_cache, const char* install_file) {
    ui = ui_;

    stop_adbd();
    set_usb_driver(true);

    ui->Print("\n\nNow send the package you want to apply\n"
              "to the device with \"adb sideload <filename>\"...\n");

    pid_t child;
    if ((child = fork()) == 0) {
        execl("/sbin/recovery", "recovery", "--adbd", install_file, NULL);
        _exit(-1);
    }
	DataManager_SetIntValue("tw_child_pid", child);
    int status;
    // TODO(dougz): there should be a way to cancel waiting for a
    // package (by pushing some button combo on the device).  For now
    // you just have to 'adb sideload' a file that's not a valid
    // package, like "/dev/null".
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ui->Print("status %d\n", WEXITSTATUS(status));
    }
	DataManager_SetIntValue("tw_has_cancel", 0); // Remove cancel button from gui now that the zip install is going to start
    set_usb_driver(false);
    maybe_restart_adbd();

    struct stat st;
    if (stat(install_file, &st) != 0) {
        if (errno == ENOENT) {
            ui->Print("No package received.\n");
        } else {
            ui->Print("Error reading package:\n  %s\n", strerror(errno));
        }
        return INSTALL_ERROR;
    }
	return TWinstall_zip(install_file, wipe_cache);
}
