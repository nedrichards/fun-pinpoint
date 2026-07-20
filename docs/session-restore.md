# Desktop session restore

Pinpoint should participate in desktop session restore through GTK's
application-state API. It should not implement an independent "reopen the last
presentation" preference in the meantime. Those are different features with
different user expectations, and a private fallback would compete with GTK's
instance lifecycle once the platform API is available.

This is serialized application state, not a suspended process image. GTK owns
registration with the desktop, periodic and shutdown saves, instance identity,
saved-state storage and cleanup, relaunch, and window-management state. Pinpoint
owns only the presentation state needed to reconstruct a safe new process.

## Platform status

The GNOME 50 SDK used by Pinpoint currently contains GTK 4.22.4. GTK registers
applications with the session manager at this version, but it does not expose
the application state-saving API. The pinned headers have `query-end`, but not
`support-save`, `autosave-interval`, `save-state`, `restore-state`,
`restore-window`, or `gtk_application_save()`.

Those APIs are being introduced as unstable GTK 4.24 API. Pinpoint should
integrate through GTK rather than calling the private
`org.gnome.SessionManager` restore methods directly. The application can opt in
once the GTK API is available; GTK and the desktop decide whether a particular
session can consume the saved state.

The current GNOME session reports `RestoreSupported=false`, and its public
`org.freedesktop.portal.Desktop` object has no application-state interface.
That confirms there is no production path to exercise with the current runtime,
but it is diagnostic information rather than an additional application-level
feature gate. Pinpoint must not inspect either interface before opting in.

Folder selections made through the FileChooser portal remain accessible across
sessions. Saving the returned folder and presentation URI in GTK's per-instance
state is therefore compatible with the Flatpak sandbox. Restore must still
handle revoked permission, moved files, and unavailable mounts without opening
a new chooser or requesting access automatically.

## Restore policy

Restore behaviour must follow `GtkRestoreReason`; it must not happen on every
launch.

| Reason | Pinpoint behaviour |
| --- | --- |
| Pristine | Restore nothing. Show the normal launch view. |
| Normal launch | Restore as little as reasonable. Keep conventionally stored preferences, but do not reopen a presentation. |
| Crash recovery | Recover the previous presentation at its saved slide, but remain windowed and paused. Do not repeat external effects. |
| Clean session restore | Reopen the presentation and saved slide. Restore speaker visibility and fullscreen only after validating the current display topology. |

GTK and the compositor own window size, position, maximized state, and other
window-management state for these snapshots. Pinpoint must not duplicate them
in its state dictionaries or GSettings. GSettings remains appropriate for real
durable preferences which apply independently of a saved desktop session.

## State contract

Each Pinpoint process represents one logical presentation session. It currently
contains an audience `GtkApplicationWindow` and a speaker
`GtkApplicationWindow`; the latter may be hidden. They share one presentation
model but are separate windows for GTK's per-window state API. This remains
compatible with `G_APPLICATION_NON_UNIQUE`: GTK's session instance identifies
each separately launched Pinpoint process.

The versioned application-level `a{sv}` state should contain the shared
presentation model:

- a schema version;
- the portal-provided presentation-folder URI and presentation URI;
- whether the launch or presentation view was active;
- the current slide index;
- comment-note handling;
- whether the speaker view was visible; and
- any other presentation option needed to parse the deck consistently.

Each application window's versioned `a{sv}` state should contain only
application-specific state for that window:

- a schema version;
- its `audience` or `speaker` role; and
- whether presentation fullscreen was requested for that role.

GTK owns the window geometry and Wayland placement associated with each saved
window. Pinpoint must not save monitor connector names as a substitute. It must
still validate the displays which exist after relaunch before applying semantic
presentation state such as speaker visibility or fullscreen.

Restoration must accept missing keys, ignore unknown keys, reject unsupported
future schema versions, clamp the slide index to the loaded deck, and fall back
to the launch view when the file or its permission is unavailable. It must not
assume that the audience window is restored first: GTK restores the most
recently used window first, which may be the speaker view. The restore
coordinator must load the shared presentation once and then create or associate
each requested window role without treating either window as a second deck.

The saved state must not include or reactivate:

- a blank screen or hidden cursor;
- a pending command or command-entry contents;
- media playback position or playing state;
- a camera stream;
- rehearsal, autoadvance, or timer state;
- an in-progress PDF export or file dialog;
- an idle or logout inhibitor; or
- live `GdkMonitor`, GStreamer, or portal object identities.

These exclusions prevent a restored or recovered process from repeating an
external command, unexpectedly activating hardware, or immediately taking over
displays.

## GTK integration gate

Implementation is blocked until Pinpoint's pinned runtime provides GTK 4.24 or
a later stable version of the same API. Once it does:

1. Set `GtkApplication:support-save` and keep GTK's default autosave interval.
2. Handle `GtkApplication::save-state` for the shared presentation model.
3. Handle `GtkApplicationWindow::save-state` for each window's role and
   semantic state; do not serialize geometry or display identities.
4. Handle `GtkApplication::restore-state` to reconstruct the shared model, and
   branch on its restore reason before opening a deck.
5. Handle `GtkApplication::restore-window` for both audience and speaker roles,
   without assuming a callback order or recreating a hidden speaker view.
6. Let GTK forget and clean instance state. Do not call the private GNOME
   session-manager interface.
7. Keep explicit command-line files authoritative over restored state.
8. Use GTK's periodic saves for ordinary progress. A manual
   `gtk_application_save()` may follow significant transitions such as opening
   or closing a deck, but must not be called for every rendered frame.

## Validation gate

The implementation is complete only with:

- application- and window-state serialization tests for missing, unknown,
  malformed, old, and future keys;
- restore-reason tests proving that normal launch does not reopen a deck;
- crash-recovery tests proving that commands, media, camera, rehearsal, and
  fullscreen do not reactivate;
- clean-restore tests for slide clamping and missing or revoked files;
- callback-order tests which restore the speaker state before the audience
  state and still create one coherent presentation;
- one- and two-display tests for safe speaker/fullscreen restoration; and
- a real GNOME session round trip proving relaunch after clean logout and safe
  fallback after a crash.
