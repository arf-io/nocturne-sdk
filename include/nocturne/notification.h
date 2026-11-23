#ifndef NOCTURNE_NOTIFICATION_H
#define NOCTURNE_NOTIFICATION_H

#include <nocturne/controls.h>
#include <nocturne/event_rules.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NT_NOTIFICATION_MAX_ACTIONS = 8, NT_NOTIFICATION_MAX_MESSAGE_ROWS = 8 };
typedef struct NtNotificationAction { uint64_t id; NtByteView label; bool disabled; } NtNotificationAction;
typedef struct NtNotificationOptions {
    uint64_t id;
    NtByteView title, message, dismiss_label;
    const NtNotificationAction *actions;
    size_t action_count;
    NtTimeUs expires_at;             /* Absolute tree time. Zero is persistent. */
    uint32_t columns, message_rows;
    NtControlStyle style;
    bool dismissible, dismiss_on_action;
} NtNotificationOptions;

typedef enum NtNotificationStatus {
    NT_NOTIFICATION_ACTIVE, NT_NOTIFICATION_EXPIRED, NT_NOTIFICATION_DISMISSED
} NtNotificationStatus;
typedef struct NtNotificationState {
    uint64_t id, selected_action;
    NtTimeUs expires_at;
    NtNotificationStatus status;
} NtNotificationState;
typedef enum NtNotificationEventKind {
    NT_NOTIFICATION_PERFORMED = 1, NT_NOTIFICATION_DISMISSED_EVENT
} NtNotificationEventKind;
typedef struct NtNotificationEvent {
    NtNotificationEventKind kind;
    uint64_t notification, action;
} NtNotificationEvent;
enum { NT_INTENT_NOTIFICATION = 0x4e4e0001 };

NtNotificationOptions nt_notification_options_default(void);
/* Copied UTF-8 title, message and action labels, with application-supplied
 * nonzero IDs. A persistent status is a card with no actions and no expiry.
 * User actions emit typed intents. Expiry and explicit dismissal are silent. */
NtResult nt_notification_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                 const NtNotificationOptions *options, NtNodeId *out);
NtResult nt_notification_set(NtTree *tree, NtNodeId node, const NtNotificationOptions *options);
NtResult nt_notification_get(const NtTree *tree, NtNodeId node, NtNotificationState *out);
NtResult nt_notification_dismiss(NtTree *tree, NtNodeId node);
NtResult nt_notification_event_decode(NtEventIntentView intent, NtNotificationEvent *out);

#ifdef __cplusplus
}
#endif
#endif
