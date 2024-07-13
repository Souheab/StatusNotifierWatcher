#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>
#include "dbus-status-notifier-watcher.h"

typedef struct {
  SnWatcher *skeleton;
  guint bus_owner_id;
  GHashTable *hosts;
  GHashTable *items;
} StatusNotifierWatcher;

static StatusNotifierWatcher status_notifier_watcher;

#define STATUSNOTIFIER_WATCHER_SERVICE "org.kde.StatusNotifierWatcher"
#define STATUSNOTIFIER_WATCHER_PATH "/StatusNotifierWatcher"
#define STATUSNOTIFIER_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"

static const gchar *const *array_from_hash_set(GHashTable *hash_table);

static gboolean on_register_host(SnWatcher *object,
                                 GDBusMethodInvocation *invocation,
                                 char *service,
                                 gpointer user_data)
{
  g_print("Registering StatusNotifierHost as %s\n", service);
  if (!g_hash_table_contains(status_notifier_watcher.hosts, service)) {
    g_hash_table_add(status_notifier_watcher.hosts, g_strdup(service));
  }
  sn_watcher_complete_register_host(object, invocation);
  sn_watcher_set_is_host_registered(object, TRUE);
  sn_watcher_emit_host_registered(object);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean on_register_item(SnWatcher *object,
                                 GDBusMethodInvocation *invocation,
                                 char *service,
                                 gpointer user_data)
{
  g_print("Registering StatusNotifierItem, service: %s\n", service);
  if (!g_hash_table_contains(status_notifier_watcher.items, service)) {
    g_hash_table_add(status_notifier_watcher.items, g_strdup(service));
  }
  const gchar *const *array = array_from_hash_set(status_notifier_watcher.items);
  sn_watcher_set_registered_items(object, array);
  sn_watcher_complete_register_item(object, invocation);
  sn_watcher_emit_item_registered(object, service);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

// TODO: Perhaps use a more efficient way
static const gchar *const *array_from_hash_set(GHashTable *hash_table) {
  const gchar **array = g_new(const gchar*, g_hash_table_size(hash_table) + 1);
  GHashTableIter iter;
  gpointer key;
  gint i = 0;

  g_hash_table_iter_init(&iter, hash_table);
  while (g_hash_table_iter_next(&iter, &key, NULL)) {
    array[i++] = key;
  }
  array[i] = NULL;

  return (const gchar *const *) array;
}

static void on_name_owner_changed(GDBusConnection *connection,
                                  const gchar* sender_name,
                                  const gchar* object_path,
                                  const gchar* interface_name,
                                  const gchar* signal_name,
                                  GVariant* parameters,
                                  gpointer user_data)
{
  g_print("Name owner changed\n");
  const gchar *name, *old_owner, *new_owner;
  g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
  g_print("Name: %s, old owner: %s, new owner: %s\n", name, old_owner, new_owner);

  if (g_hash_table_contains(status_notifier_watcher.items, name) && new_owner[0] == '\0') {
    g_print("Removing StatusNotifierItem %s\n", name);
    g_hash_table_remove(status_notifier_watcher.items, name);
    const gchar *const *array = array_from_hash_set(status_notifier_watcher.items);
    sn_watcher_set_registered_items(status_notifier_watcher.skeleton, array);
    sn_watcher_emit_item_unregistered(status_notifier_watcher.skeleton, name);
  }

  if (g_hash_table_contains(status_notifier_watcher.hosts, name) && new_owner[0] == '\0') {
    g_print("Removing StatusNotifierHost %s\n", name);
    g_hash_table_remove(status_notifier_watcher.hosts, name);
    if (g_hash_table_size(status_notifier_watcher.hosts) == 0) {
      sn_watcher_set_is_host_registered(status_notifier_watcher.skeleton, FALSE);
    }
  }
}

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar *name,
                            gpointer user_data)
{
  GError *error = NULL;

  g_print("Bus acquired: %s\n", name);

  g_signal_connect(status_notifier_watcher.skeleton,
                   "handle-register-host",
                   G_CALLBACK(on_register_host),
                   NULL);

  g_signal_connect(status_notifier_watcher.skeleton,
                   "handle-register-item",
                   G_CALLBACK(on_register_item),
                   NULL);

  g_dbus_connection_signal_subscribe(
      connection,
      "org.freedesktop.DBus",
      "org.freedesktop.DBus",
      "NameOwnerChanged",
      "/org/freedesktop/DBus",
      NULL,
      G_DBUS_SIGNAL_FLAGS_NONE,
      on_name_owner_changed,
      NULL,
      NULL);
      

  if (!g_dbus_interface_skeleton_export(
          G_DBUS_INTERFACE_SKELETON(status_notifier_watcher.skeleton),
          connection,
          STATUSNOTIFIER_WATCHER_PATH,
          &error))
    {
      g_warning("Error exporting StatusNotifierWatcher skeleton: %s",
                error->message);
      g_error_free(error);
      return;
    }
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_print("Name acquired: %s\n", name);
}
static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  g_print("Name lost: %s\n", name);
}

static gboolean setup_dbus(gpointer data) {
  g_print("Setting up dbus\n");

  status_notifier_watcher = (StatusNotifierWatcher) {
    .skeleton = sn_watcher_skeleton_new(),
    .bus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                     STATUSNOTIFIER_WATCHER_SERVICE,
                     G_BUS_NAME_OWNER_FLAGS_NONE,
                     on_bus_acquired,
                     on_name_acquired,
                     on_name_lost,
                     NULL,
                     NULL),
    .hosts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL),
    .items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL),
  };

  sn_watcher_set_protocol_version(status_notifier_watcher.skeleton, 1);
  sn_watcher_set_is_host_registered(status_notifier_watcher.skeleton, FALSE);
  sn_watcher_set_registered_items(status_notifier_watcher.skeleton, NULL);

  return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    GMainLoop *loop;

    g_idle_add(setup_dbus, NULL);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_bus_unown_name(status_notifier_watcher.bus_owner_id);
    g_hash_table_destroy(status_notifier_watcher.hosts);
    g_hash_table_destroy(status_notifier_watcher.items);
    g_object_unref(status_notifier_watcher.skeleton);

    return 0;
}
