#include "host/Tray.h"

#include <unistd.h>

namespace sukut::host {
namespace {

constexpr const char* kItemPath = "/StatusNotifierItem";
constexpr const char* kItemInterface = "org.kde.StatusNotifierItem";
constexpr const char* kMenuPath = "/MenuBar";
constexpr const char* kMenuInterface = "com.canonical.dbusmenu";
constexpr const char* kAppId = "io.github.altarafiya.Sukut";

constexpr const char* kItemXml = R"xml(<node><interface name="org.kde.StatusNotifierItem">
  <property name="Category" type="s" access="read"/><property name="Id" type="s" access="read"/>
  <property name="Title" type="s" access="read"/><property name="Status" type="s" access="read"/>
  <property name="WindowId" type="i" access="read"/><property name="IconName" type="s" access="read"/>
  <property name="IconThemePath" type="s" access="read"/><property name="AttentionIconName" type="s" access="read"/>
  <property name="ToolTip" type="(sa(iiay)ss)" access="read"/><property name="ItemIsMenu" type="b" access="read"/>
  <property name="Menu" type="o" access="read"/>
  <method name="ContextMenu"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
  <method name="Activate"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
  <method name="SecondaryActivate"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
  <method name="Scroll"><arg name="delta" type="i" direction="in"/><arg name="orientation" type="s" direction="in"/></method>
  <method name="ProvideXdgActivationToken"><arg name="token" type="s" direction="in"/></method>
  <signal name="NewTitle"/><signal name="NewIcon"/><signal name="NewAttentionIcon"/><signal name="NewToolTip"/>
  <signal name="NewStatus"><arg name="status" type="s"/></signal>
</interface></node>)xml";

constexpr const char* kMenuXml = R"xml(<node><interface name="com.canonical.dbusmenu">
  <property name="Version" type="u" access="read"/><property name="TextDirection" type="s" access="read"/>
  <property name="Status" type="s" access="read"/><property name="IconThemePath" type="as" access="read"/>
  <method name="GetLayout"><arg type="i" name="parentId" direction="in"/><arg type="i" name="recursionDepth" direction="in"/>
    <arg type="as" name="propertyNames" direction="in"/><arg type="u" name="revision" direction="out"/>
    <arg type="(ia{sv}av)" name="layout" direction="out"/></method>
  <method name="GetGroupProperties"><arg type="ai" name="ids" direction="in"/><arg type="as" name="propertyNames" direction="in"/>
    <arg type="a(ia{sv})" name="properties" direction="out"/></method>
  <method name="GetProperty"><arg type="i" name="id" direction="in"/><arg type="s" name="name" direction="in"/>
    <arg type="v" name="value" direction="out"/></method>
  <method name="Event"><arg type="i" name="id" direction="in"/><arg type="s" name="eventId" direction="in"/>
    <arg type="v" name="data" direction="in"/><arg type="u" name="timestamp" direction="in"/></method>
  <method name="EventGroup"><arg type="a(isvu)" name="events" direction="in"/><arg type="ai" name="idErrors" direction="out"/></method>
  <method name="AboutToShow"><arg type="i" name="id" direction="in"/><arg type="b" name="needUpdate" direction="out"/></method>
  <method name="AboutToShowGroup"><arg type="ai" name="ids" direction="in"/><arg type="ai" name="updatesNeeded" direction="out"/>
    <arg type="ai" name="idErrors" direction="out"/></method>
  <signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})" name="updatedProps"/><arg type="a(ias)" name="removedProps"/></signal>
  <signal name="LayoutUpdated"><arg type="u" name="revision"/><arg type="i" name="parent"/></signal>
  <signal name="ItemActivationRequested"><arg type="i" name="id"/><arg type="u" name="timestamp"/></signal>
</interface></node>)xml";

Tray* Self(gpointer data) { return static_cast<Tray*>(data); }

void ItemCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method, GVariant* parameters,
              GDBusMethodInvocation* invocation, gpointer data) {
  Self(data)->OnItemMethod(method, parameters, invocation);
}
GVariant* ItemGet(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property, GError**, gpointer data) {
  return Self(data)->OnItemProperty(property);
}
void MenuCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method, GVariant* parameters,
              GDBusMethodInvocation* invocation, gpointer data) {
  Self(data)->OnMenuMethod(method, parameters, invocation);
}
GVariant* MenuGet(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property, GError**, gpointer data) {
  return Self(data)->OnMenuProperty(property);
}
const GDBusInterfaceVTable kItemVTable{ItemCall, ItemGet, nullptr, {}};
const GDBusInterfaceVTable kMenuVTable{MenuCall, MenuGet, nullptr, {}};

void WatcherAppeared(GDBusConnection*, const gchar*, const gchar*, gpointer data) { Self(data)->OnWatcher(true); }
void WatcherVanished(GDBusConnection*, const gchar*, gpointer data) { Self(data)->OnWatcher(false); }

const char* T(bool arabic, const char* english, const char* arabic_text) { return arabic ? arabic_text : english; }

}  // namespace

Tray::Tray(Actions actions, std::string icon_theme_path)
    : actions_(std::move(actions)), icon_theme_path_(std::move(icon_theme_path)) {
  bus_name_ = "org.kde.StatusNotifierItem-" + std::to_string(::getpid()) + "-1";
}

Tray::~Tray() {
  if (connection_ == nullptr) return;
  if (watch_id_) g_bus_unwatch_name(watch_id_);
  if (name_id_) g_bus_unown_name(name_id_);
  if (item_object_) g_dbus_connection_unregister_object(connection_, item_object_);
  if (menu_object_) g_dbus_connection_unregister_object(connection_, menu_object_);
  if (item_info_) g_dbus_node_info_unref(item_info_);
  if (menu_info_) g_dbus_node_info_unref(menu_info_);
  g_object_unref(connection_);
}

bool Tray::Start(GDBusConnection* connection, std::string& error) {
  connection_ = G_DBUS_CONNECTION(g_object_ref(connection));
  GError* failure = nullptr;
  item_info_ = g_dbus_node_info_new_for_xml(kItemXml, &failure);
  if (item_info_ != nullptr) menu_info_ = g_dbus_node_info_new_for_xml(kMenuXml, &failure);
  if (menu_info_ != nullptr)
    item_object_ = g_dbus_connection_register_object(connection_, kItemPath, item_info_->interfaces[0], &kItemVTable, this, nullptr, &failure);
  if (item_object_ != 0)
    menu_object_ = g_dbus_connection_register_object(connection_, kMenuPath, menu_info_->interfaces[0], &kMenuVTable, this, nullptr, &failure);
  if (menu_object_ == 0) {
    error = failure ? failure->message : "cannot export the tray";
    if (failure) g_error_free(failure);
    return false;
  }
  name_id_ = g_bus_own_name_on_connection(connection_, bus_name_.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr, nullptr, nullptr);
  watch_id_ = g_bus_watch_name_on_connection(connection_, "org.kde.StatusNotifierWatcher", G_BUS_NAME_WATCHER_FLAGS_NONE,
                                             WatcherAppeared, WatcherVanished, this, nullptr);
  return true;
}

void Tray::OnWatcher(bool present) {
  if (!present) {
    const bool was = registered_;
    registered_ = false;
    if (was && actions_.availability) actions_.availability(false);
    return;
  }
  g_dbus_connection_call(
      connection_, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
      "RegisterStatusNotifierItem", g_variant_new("(s)", bus_name_.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer data) {
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        if (reply) g_variant_unref(reply);
        if (error) g_error_free(error);
        Self(data)->OnRegistered(reply != nullptr);
      },
      this);
}

void Tray::OnRegistered(bool ok) {
  registered_ = ok;
  if (actions_.availability) actions_.availability(ok);
}

std::string Tray::IconName() const {
  // The tray keeps the س glyph: gold while filtering, symbolic (panel-coloured) when Off.
  return state_.on ? std::string(kAppId) + "-tray-on" : std::string(kAppId) + "-tray-idle-symbolic";
}

void Tray::Update(const State& state) {
  state_ = state;
  Build(state);
  ++revision_;
  if (connection_ == nullptr) return;
  const std::string icon = IconName();
  const std::string status = state.attention ? "NeedsAttention" : "Active";
  if (icon != icon_) { icon_ = icon; Emit(kItemPath, kItemInterface, "NewIcon", nullptr); }
  if (status != status_) { status_ = status; Emit(kItemPath, kItemInterface, "NewStatus", g_variant_new("(s)", status.c_str())); }
  if (state.status != tooltip_) { tooltip_ = state.status; Emit(kItemPath, kItemInterface, "NewToolTip", nullptr); }
  Emit(kMenuPath, kMenuInterface, "LayoutUpdated", g_variant_new("(ui)", revision_, 0));
}

void Tray::Build(const State& s) {
  const bool ar = s.arabic;
  items_.clear();
  const bool usable = !s.busy;
  auto add = [this](int id, Item item) { items_[id] = std::move(item); };
  auto separator = [](void) { Item i; i.type = "separator"; return i; };

  Item status;
  status.label = s.status;
  status.enabled = false;
  if (s.standby) status.icon = std::string(kAppId) + "-standby-symbolic";
  add(1, status);
  add(2, separator());
  Item open;
  open.label = T(ar, "Open Sukut", "فتح سُكوت");
  open.action = [this] { if (actions_.open) actions_.open(); };
  add(13, open);
  Item exit;
  exit.label = T(ar, "Exit", "خروج");
  exit.action = [this] { if (actions_.exit) actions_.exit(); };
  add(14, exit);

  Item root;
  if (s.window_open) {
    // The window is the one place to control Sukut while it is open.
    root.children = {13, 14};
    add(0, root);
    return;
  }

  Item power;
  power.label = T(ar, "Sukut on", "تشغيل سُكوت");
  power.toggle = "checkmark";
  power.toggle_state = s.on ? 1 : 0;
  power.enabled = usable;
  power.action = [this, on = s.on] { if (actions_.power) actions_.power(!on); };
  add(3, power);
  add(4, separator());
  const char* modes[3][2] = {{"Music Filter", "مرشّح الموسيقى"}, {"Optimized", "محسّن"}, {"Strict", "صارم"}};
  const int shown = s.on ? s.mode : s.preferred;
  for (int m = 1; m <= 3; ++m) {
    Item mode;
    mode.label = T(ar, modes[m - 1][0], modes[m - 1][1]);
    mode.toggle = "radio";
    mode.toggle_state = shown == m ? 1 : 0;
    mode.enabled = usable;
    mode.action = [this, m] { if (actions_.select_mode) actions_.select_mode(m); };
    add(4 + m, mode);
  }
  add(8, separator());

  Item buffering;
  buffering.label = T(ar, "Audio buffering", "التخزين المؤقت للصوت");
  buffering.enabled = usable;
  const char* levels[3][2] = {{"Lowest", "الأقل"}, {"Balanced", "متوازن"}, {"Maximum", "الأكبر"}};
  for (int level = 0; level < 3; ++level) {
    Item choice;
    choice.label = T(ar, levels[level][0], levels[level][1]);
    choice.toggle = "radio";
    choice.toggle_state = s.buffering == level ? 1 : 0;
    choice.action = [this, level] { if (actions_.set_buffering) actions_.set_buffering(level); };
    add(100 + level, choice);
    buffering.children.push_back(100 + level);
  }
  add(9, buffering);

  Item output;
  output.label = T(ar, "Audio output", "جهاز الصوت");
  output.enabled = usable;
  for (std::size_t i = 0; i < s.outputs.size() && i < 90; ++i) {
    Item choice;
    choice.label = s.outputs[i].second;
    choice.toggle = "radio";
    choice.toggle_state = s.outputs[i].first == s.output ? 1 : 0;
    choice.action = [this, name = s.outputs[i].first] { if (actions_.set_output) actions_.set_output(name); };
    add(200 + static_cast<int>(i), choice);
    output.children.push_back(200 + static_cast<int>(i));
  }
  add(10, output);

  Item source;
  source.label = T(ar, "Source app", "التطبيق المصدر");
  source.enabled = usable;
  Item all;
  all.label = T(ar, "All system audio", "صوت النظام بالكامل");
  all.toggle = "radio";
  all.toggle_state = s.source_pid == 0 ? 1 : 0;
  all.action = [this] { if (actions_.set_source) actions_.set_source(0); };
  add(300, all);
  source.children.push_back(300);
  for (std::size_t i = 0; i < s.apps.size() && i < 90; ++i) {
    Item choice;
    choice.label = s.apps[i].second;
    choice.toggle = "radio";
    choice.toggle_state = s.apps[i].first == s.source_pid ? 1 : 0;
    choice.action = [this, pid = s.apps[i].first] { if (actions_.set_source) actions_.set_source(pid); };
    add(301 + static_cast<int>(i), choice);
    source.children.push_back(301 + static_cast<int>(i));
  }
  add(11, source);
  add(12, separator());
  root.children = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  add(0, root);
}

GVariant* Tray::Properties(int id) const {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
  const auto found = items_.find(id);
  if (found != items_.end()) {
    const Item& item = found->second;
    if (!item.type.empty()) g_variant_builder_add(&builder, "{sv}", "type", g_variant_new_string(item.type.c_str()));
    if (!item.label.empty()) g_variant_builder_add(&builder, "{sv}", "label", g_variant_new_string(item.label.c_str()));
    g_variant_builder_add(&builder, "{sv}", "enabled", g_variant_new_boolean(item.enabled));
    g_variant_builder_add(&builder, "{sv}", "visible", g_variant_new_boolean(item.visible));
    if (!item.toggle.empty()) {
      g_variant_builder_add(&builder, "{sv}", "toggle-type", g_variant_new_string(item.toggle.c_str()));
      g_variant_builder_add(&builder, "{sv}", "toggle-state", g_variant_new_int32(item.toggle_state));
    }
    if (!item.children.empty()) g_variant_builder_add(&builder, "{sv}", "children-display", g_variant_new_string("submenu"));
    if (!item.icon.empty()) g_variant_builder_add(&builder, "{sv}", "icon-name", g_variant_new_string(item.icon.c_str()));
  }
  return g_variant_builder_end(&builder);
}

GVariant* Tray::Layout(int id, int depth) const {
  GVariantBuilder children;
  g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
  const auto found = items_.find(id);
  if (found != items_.end() && depth != 0)
    for (const int child : found->second.children)
      g_variant_builder_add(&children, "v", Layout(child, depth < 0 ? -1 : depth - 1));
  return g_variant_new("(i@a{sv}@av)", id, Properties(id), g_variant_builder_end(&children));
}

void Tray::Emit(const char* path, const char* interface, const char* signal, GVariant* parameters) {
  g_dbus_connection_emit_signal(connection_, nullptr, path, interface, signal, parameters, nullptr);
}

GVariant* Tray::OnItemProperty(const gchar* property) {
  const std::string p = property;
  if (p == "Category") return g_variant_new_string("ApplicationStatus");
  if (p == "Id") return g_variant_new_string(kAppId);
  if (p == "Title") return g_variant_new_string("Sukut");
  if (p == "Status") return g_variant_new_string(state_.attention ? "NeedsAttention" : "Active");
  if (p == "WindowId") return g_variant_new_int32(0);
  if (p == "IconName") return g_variant_new_string(IconName().c_str());
  if (p == "IconThemePath") return g_variant_new_string(icon_theme_path_.c_str());
  if (p == "AttentionIconName") return g_variant_new_string((std::string(kAppId) + "-tray-on").c_str());
  if (p == "ItemIsMenu") return g_variant_new_boolean(FALSE);
  if (p == "Menu") return g_variant_new_object_path(kMenuPath);
  if (p == "ToolTip")
    return g_variant_new("(s@a(iiay)ss)", "", g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0),
                         "سُكوت Sukut", state_.status.c_str());
  return nullptr;
}

void Tray::OnItemMethod(const gchar* method, GVariant* parameters, GDBusMethodInvocation* invocation) {
  const std::string m = method;
  if (m == "Activate") { if (actions_.activate) actions_.activate(); }
  else if (m == "ProvideXdgActivationToken") {
    const gchar* token = nullptr;
    g_variant_get(parameters, "(&s)", &token);
    if (actions_.activation_token && token) actions_.activation_token(token);
  }
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

GVariant* Tray::OnMenuProperty(const gchar* property) {
  const std::string p = property;
  if (p == "Version") return g_variant_new_uint32(3);
  if (p == "TextDirection") return g_variant_new_string(state_.arabic ? "rtl" : "ltr");
  if (p == "Status") return g_variant_new_string("normal");
  if (p == "IconThemePath") {
    const gchar* paths[] = {icon_theme_path_.c_str(), nullptr};
    return g_variant_new_strv(icon_theme_path_.empty() ? nullptr : paths, icon_theme_path_.empty() ? 0 : 1);
  }
  return nullptr;
}

void Tray::OnMenuMethod(const gchar* method, GVariant* parameters, GDBusMethodInvocation* invocation) {
  const std::string m = method;
  if (m == "GetLayout") {
    gint32 parent = 0, depth = -1;
    g_variant_get(parameters, "(ii@as)", &parent, &depth, nullptr);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", revision_, Layout(parent, depth)));
  } else if (m == "GetGroupProperties") {
    GVariantIter* ids = nullptr;
    g_variant_get(parameters, "(ai@as)", &ids, nullptr);
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ia{sv})"));
    gint32 id = 0;
    while (g_variant_iter_next(ids, "i", &id))
      if (items_.count(id)) g_variant_builder_add(&builder, "(i@a{sv})", id, Properties(id));
    g_variant_iter_free(ids);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&builder)));
  } else if (m == "GetProperty") {
    gint32 id = 0;
    const gchar* name = nullptr;
    g_variant_get(parameters, "(i&s)", &id, &name);
    GVariant* properties = Properties(id);
    g_variant_ref_sink(properties);
    GVariant* value = g_variant_lookup_value(properties, name, nullptr);
    g_variant_unref(properties);
    if (value == nullptr) {
      g_dbus_method_invocation_return_dbus_error(invocation, "com.canonical.dbusmenu.Error", "no such property");
      return;
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
  } else if (m == "Event" || m == "EventGroup") {
    std::vector<std::pair<int, std::string>> events;
    if (m == "Event") {
      gint32 id = 0;
      const gchar* event = nullptr;
      g_variant_get(parameters, "(i&s@vu)", &id, &event, nullptr, nullptr);
      events.emplace_back(id, event);
    } else {
      GVariantIter* list = nullptr;
      g_variant_get(parameters, "(a(isvu))", &list);
      gint32 id = 0;
      const gchar* event = nullptr;
      while (g_variant_iter_next(list, "(i&s@vu)", &id, &event, nullptr, nullptr)) events.emplace_back(id, event);
      g_variant_iter_free(list);
    }
    // Reply first: the action may rebuild the menu.
    if (m == "Event") g_dbus_method_invocation_return_value(invocation, nullptr);
    else g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
    for (const auto& [id, event] : events) {
      const auto found = items_.find(id);
      if (event == "clicked" && found != items_.end() && found->second.enabled && found->second.action) {
        const auto action = found->second.action;  // a copy: the action may rebuild items_
        action();
      }
    }
  } else if (m == "AboutToShow") {
    if (actions_.refresh) actions_.refresh();
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
  } else if (m == "AboutToShowGroup") {
    if (actions_.refresh) actions_.refresh();
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0),
                                                                    g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
  } else {
    g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod", method);
  }
}

}  // namespace sukut::host
