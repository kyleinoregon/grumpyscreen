#ifndef UPDATE_PROGRESS_H
#define UPDATE_PROGRESS_H

#ifdef UPDATE_BUTTON_CMD

#include "lvgl.h"
#include "logger.h"
#include "simple_dialog.h"
#include "subprocess.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

// How often the status file written by the update command is re-read.
static constexpr uint32_t UPDATE_PROGRESS_POLL_MS = 500;

struct UpdateProgressCtx {
    lv_obj_t *mbox = nullptr;
    lv_obj_t *bar = nullptr;
    lv_obj_t *label = nullptr;
    std::unique_ptr<subprocess::Popen> proc;
    bool saw_reboot = false;
};

// Reads the "<state>|<percent>" line the update command publishes. Returns
// false while the file does not exist yet, which is the normal case for the
// first poll or two.
static inline bool update_progress_read(std::string &state, int &percent) {
    FILE *f = fopen(UPDATE_BUTTON_STATUS_FILE, "r");
    if (f == nullptr) return false;

    char buf[64] = {0};
    char *line = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (line == nullptr) return false;

    char *sep = strchr(buf, '|');
    if (sep == nullptr) return false;
    *sep = '\0';

    state = buf;
    percent = atoi(sep + 1);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return true;
}

static inline void update_progress_fail(UpdateProgressCtx *ctx, lv_timer_t *t) {
    simple_dialog_close(ctx->mbox);
    create_simple_dialog(lv_scr_act(),
                         UPDATE_BUTTON_TITLE " Failed",
                         UPDATE_BUTTON_FAILURE,
                         true,
                         true);
    delete ctx;
    lv_timer_del(t);
}

static inline void update_progress_timer_cb(lv_timer_t *t) {
    UpdateProgressCtx *ctx = static_cast<UpdateProgressCtx *>(t->user_data);

    std::string state;
    int percent = 0;
    if (update_progress_read(state, percent)) {
        if (state == "downloading") {
            lv_bar_set_value(ctx->bar, percent, LV_ANIM_OFF);
            if (percent > 0) {
                lv_label_set_text_fmt(ctx->label, "Downloading update... %d%%", percent);
            } else {
                // Nothing counted yet, or the size of the download is unknown.
                lv_label_set_text(ctx->label, "Downloading update...");
            }
        } else if (state == "flashing") {
            lv_bar_set_value(ctx->bar, 100, LV_ANIM_OFF);
            lv_label_set_text(ctx->label, "Installing update, do not power off!");
        } else if (state == "rebooting") {
            ctx->saw_reboot = true;
            lv_bar_set_value(ctx->bar, 100, LV_ANIM_OFF);
            lv_label_set_text(ctx->label, UPDATE_BUTTON_SUCCESS);
        }
        // "failed" needs no handling of its own: the command exits straight
        // after writing it, and the exit status below is what we act on.
    }

    // The update command ends in a reboot, so observing it exit before it
    // reported "rebooting" means the update did not get that far.
    int ret;
    try {
        ret = ctx->proc->poll();
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to poll update process: {}", e.what());
        update_progress_fail(ctx, t);
        return;
    }

    if (ret >= 0) {
        if (ctx->saw_reboot) {
            // Leave the dialog up and wait for the machine to go down.
            delete ctx;
            lv_timer_del(t);
        } else {
            update_progress_fail(ctx, t);
        }
    }
}

// Runs the update command detached and shows a dialog with a progress bar fed
// by the status file the command writes (UPDATE_BUTTON_STATUS_FILE, see the
// Makefile). Unlike a blocking call from the click handler this leaves the UI
// thread free, so the dialog is drawn immediately and keeps updating for the
// length of the download.
static inline void start_update_with_progress(const std::string &cmd) {
    // Discard any status left behind by a previous attempt.
    remove(UPDATE_BUTTON_STATUS_FILE);

    // The taller dialog variant: with the bar under it the message needs more
    // room than the one line dialog offers on a 272 pixel high screen.
    SimpleDialogOptions options{};
    options.multiline_message = true;
    lv_obj_t *mbox = create_configurable_dialog(lv_scr_act(),
                                                UPDATE_BUTTON_TITLE " Initiated",
                                                "Starting update...",
                                                options);

    lv_obj_t *content = lv_msgbox_get_content(mbox);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Gap between the message text and the bar below it.
    lv_obj_set_style_pad_row(content, 12, 0);

    lv_obj_t *bar = lv_bar_create(content);
    lv_obj_set_width(bar, LV_PCT(90));
    lv_obj_set_height(bar, 18);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    UpdateProgressCtx *ctx = new UpdateProgressCtx();
    ctx->mbox = mbox;
    ctx->bar = bar;
    ctx->label = lv_msgbox_get_text(mbox);

    try {
        // Same invocation as call_command(): the configured command is split
        // on whitespace and executed directly, we just do not wait for it.
        ctx->proc.reset(new subprocess::Popen(cmd));
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to start update command '{}': {}", cmd, e.what());
        simple_dialog_close(mbox);
        create_simple_dialog(lv_scr_act(),
                             UPDATE_BUTTON_TITLE " Failed",
                             UPDATE_BUTTON_FAILURE,
                             true,
                             true);
        delete ctx;
        return;
    }

    lv_timer_create(update_progress_timer_cb, UPDATE_PROGRESS_POLL_MS, ctx);
}

#endif // UPDATE_BUTTON_CMD
#endif // UPDATE_PROGRESS_H
