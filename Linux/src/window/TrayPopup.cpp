#include "window/TrayPopup.h"
#include "window/HostLink.h"
#include "window/TrayPlacement.h"
#include "window/Model.h"
#include "window/Style.h"
#include "window/Widgets.h"
#include <adwaita.h>
#ifdef SUKUT_HAS_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace sukut::window {
namespace {
GtkWidget* IconLabel(const char* icon,const char* text) {
  auto* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
  gtk_box_append(GTK_BOX(row),Icon(icon));gtk_box_append(GTK_BOX(row),Label(text,nullptr,false));return row;
}
std::string Db(int value) {return (value>0?"+":"")+std::to_string(value)+" dB";}
struct Popup {
  GtkApplication* app=nullptr;GtkWidget* window=nullptr;GtkWidget* canvas=nullptr;GtkWidget* card=nullptr;
  GtkWidget* scroller=nullptr;GdkRectangle monitor_bounds{};bool native_focused=false,first_frame=false,reading=false,catalog_pending=false,choices_dirty=false;
  GtkWidget* status=nullptr;GtkWidget* power=nullptr;GtkWidget* gain_box=nullptr;
  GtkWidget* gain_value=nullptr;GtkWidget* gain_scale=nullptr;
  std::array<GtkWidget*,3> modes{},buffers{},advanced_values{},advanced_scales{};
  std::array<bool,4> dragging{};
  std::unique_ptr<HostLink> link;HostState state;Catalog catalog;
  GtkWidget* output_summary=nullptr;GtkWidget* source_summary=nullptr;
  GtkWidget* output_box=nullptr;GtkWidget* source_box=nullptr;GtkWidget* output_title=nullptr;GtkWidget* source_title=nullptr;
  int pending_mode=-1;bool applying=false,command=false;guint poll=0;int x=0,y=0;bool capture=false,unsupported=false;
  std::vector<std::vector<std::string>> captured_commands;
  ~Popup(){if(poll)g_source_remove(poll);link.reset();}
  const char* L(const char* en,const char* ar)const{return T(state.Arabic(),en,ar);}
  void Close(){g_application_quit(G_APPLICATION(app));}
  void Send(std::vector<std::string> fields) {
    if(capture){captured_commands.push_back(std::move(fields));pending_mode=-1;return;}
    if(!link)return;
    command=true;Render();
    link->Send(std::move(fields),[this](bool,const std::string&){command=false;pending_mode=-1;if(choices_dirty){choices_dirty=false;Choices();}Refresh();});
  }
  void Select(int mode) {
    Send({"select",std::to_string(mode),state.chosen_output,std::to_string(state.source_pid),std::to_string(state.buffering),
      std::to_string(state.background_db),state.advanced_optimized?"1":"0",std::to_string(state.advanced_voice_db),
      std::to_string(state.advanced_music_db),std::to_string(state.advanced_background_db)});
  }
  void Set(const std::string& key,const std::string& value) {Send({"settings",key+"="+value});}
  void ApplyChange(const std::string& key,const std::string& value) {if(state.mode)Select(state.mode);else Set(key,value);}
  void Refresh() {
    if(!link || reading || command)return;
    reading=true;
    link->Send({"read"},[this](bool ok,const std::string& payload){reading=false;if(ok){auto fresh=HostState::From(Json::Parse(payload));if(fresh.language!=state.language || fresh.theme!=state.theme || fresh.advanced_optimized!=state.advanced_optimized){Close();return;}const bool choice_changed=fresh.source_pid!=state.source_pid || fresh.chosen_output!=state.chosen_output;state=fresh;if(choice_changed)Choices();Render();}});
  }
  void CatalogNow(bool refresh=false) {
    if(!link || catalog_pending)return;
    catalog_pending=true;
    link->Send({refresh?"apps":"tray-catalog"},[this](bool ok,const std::string& payload){catalog_pending=false;if(ok){catalog=Catalog::From(Json::Parse(payload));Choices();Render();}});
  }
  GtkWidget* Button(const char* title,const char* icon,std::function<void()> action,bool toggle=false) {
    GtkWidget* b=toggle?gtk_toggle_button_new():gtk_button_new();gtk_button_set_child(GTK_BUTTON(b),IconLabel(icon,title));
    gtk_widget_add_css_class(b,"flat");
    auto* call=new std::function<void()>([this,b,toggle,action=std::move(action)]{if(applying || (toggle && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b))))return;action();});
    g_signal_connect_data(b,"clicked",G_CALLBACK((+[](GtkButton*,gpointer p){(*static_cast<std::function<void()>*>(p))();})),call,
      +[](gpointer p,GClosure*){delete static_cast<std::function<void()>*>(p);},G_CONNECT_DEFAULT);
    return b;
  }
  GtkWidget* Scale(int stem,GtkWidget* label) {
    GtkWidget* scale=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,-20,10,1);
    gtk_scale_set_draw_value(GTK_SCALE(scale),FALSE);gtk_range_set_round_digits(GTK_RANGE(scale),0);gtk_range_set_increments(GTK_RANGE(scale),1,5);
    for(int db=-20;db<=10;db+=5)gtk_scale_add_mark(GTK_SCALE(scale),db,GTK_POS_BOTTOM,nullptr);
    struct Drag{Popup* owner;int stem;GtkWidget* scale;GtkWidget* label;};auto* drag=new Drag{this,stem,scale,label};
    g_object_set_data_full(G_OBJECT(scale),"drag",drag,[](gpointer p){delete static_cast<Drag*>(p);});
    g_signal_connect(scale,"value-changed",G_CALLBACK((+[](GtkRange* r,gpointer p){
      auto* d=static_cast<Drag*>(p);if(d->owner->applying)return;
      gtk_label_set_text(GTK_LABEL(d->label),Db(static_cast<int>(std::lround(gtk_range_get_value(r)))).c_str());
      if(!d->owner->dragging[d->stem+1])d->owner->Gain(d->stem,static_cast<int>(std::lround(gtk_range_get_value(r))));
    })),drag);
    GtkEventController* events=gtk_event_controller_legacy_new();gtk_event_controller_set_propagation_phase(events,GTK_PHASE_CAPTURE);
    g_signal_connect(events,"event",G_CALLBACK((+[](GtkEventControllerLegacy*,GdkEvent* e,gpointer p)->gboolean {
      auto*d=static_cast<Drag*>(p);const auto type=gdk_event_get_event_type(e);
      if(type==GDK_BUTTON_PRESS || type==GDK_TOUCH_BEGIN)d->owner->dragging[d->stem+1]=true;
      if(type==GDK_BUTTON_RELEASE || type==GDK_TOUCH_END || type==GDK_TOUCH_CANCEL) {
        const bool held=d->owner->dragging[d->stem+1];d->owner->dragging[d->stem+1]=false;
        if(held)d->owner->Gain(d->stem,static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(d->scale)))));
      }return FALSE;
    })),drag);gtk_widget_add_controller(scale,events);return scale;
  }
  void Gain(int stem,int value) {
    value=std::clamp(value,-20,10);
    if(stem<0){if(state.background_db==value)return;state.background_db=value;ApplyChange("OptimizedBackgroundDb",std::to_string(value));}
    else {int& saved=stem==0?state.advanced_voice_db:stem==1?state.advanced_music_db:state.advanced_background_db;
      if(saved==value)return;
      saved=value;ApplyChange(stem==0?"AdvancedVoiceDb":stem==1?"AdvancedMusicDb":"AdvancedBackgroundDb",std::to_string(value));}
  }
  GtkWidget* GainRow(const char* name,const char* icon,const char* tone,GtkWidget** value) {
    GtkWidget* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_append(GTK_BOX(row),Icon(icon,tone));
    auto* title=Label(name,tone,false);gtk_widget_set_hexpand(title,TRUE);gtk_box_append(GTK_BOX(row),title);
    *value=Label("",tone,false);gtk_widget_set_direction(*value,GTK_TEXT_DIR_LTR);gtk_box_append(GTK_BOX(row),*value);return row;
  }
  GtkWidget* ChoiceHeading(const char* title,const char* icon,GtkWidget** summary) {
    auto* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);gtk_box_append(GTK_BOX(row),Icon(icon,"accent-text"));
    auto* names=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_box_append(GTK_BOX(names),Label(title,"heading",false));
    *summary=Label("","dim-label",false);gtk_label_set_ellipsize(GTK_LABEL(*summary),PANGO_ELLIPSIZE_END);gtk_label_set_max_width_chars(GTK_LABEL(*summary),30);gtk_box_append(GTK_BOX(names),*summary);gtk_box_append(GTK_BOX(row),names);return row;
  }
  void Build() {
    gtk_widget_set_default_direction(state.Arabic()?GTK_TEXT_DIR_RTL:GTK_TEXT_DIR_LTR);gtk_widget_set_direction(window,state.Arabic()?GTK_TEXT_DIR_RTL:GTK_TEXT_DIR_LTR);SetClass(window,"arabic",state.Arabic());
    card=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);gtk_widget_add_css_class(card,"tray-flyout");gtk_widget_set_size_request(card,340,-1);
    GtkWidget* header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
    gtk_box_append(GTK_BOX(header),Icon("io.github.altarafiya.Sukut-tray-on","accent-text"));
    auto* names=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_widget_set_hexpand(names,TRUE);
    gtk_box_append(GTK_BOX(names),Label("Sukut · سُكوت","heading",false));status=Label("","dim-label",false);gtk_box_append(GTK_BOX(names),status);gtk_box_append(GTK_BOX(header),names);
    power=gtk_switch_new();gtk_widget_set_valign(power,GTK_ALIGN_CENTER);Name(power,L("Turn Sukut on or off","تشغيل سُكوت أو إيقافه"));
    g_signal_connect(power,"state-set",G_CALLBACK((+[](GtkSwitch*,gboolean on,gpointer p)->gboolean{auto*s=static_cast<Popup*>(p);if(!s->applying)s->Select(on?s->state.preferred_mode:0);return FALSE;})),this);
    gtk_box_append(GTK_BOX(header),power);gtk_box_append(GTK_BOX(card),header);
    GtkWidget* tiles=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_set_homogeneous(GTK_BOX(tiles),TRUE);
    const char* icons[]={"io.github.altarafiya.Sukut-music-filter-symbolic","io.github.altarafiya.Sukut-sliders-symbolic","changes-prevent-symbolic"};
    const char* titles[]={L("Music Filter","مرشّح الموسيقى"),L("Optimized","محسّن"),L("Strict","صارم")};
    for(int i=0;i<3;++i){
      const int mode=i+1;auto* b=Button(titles[i],icons[i],[this,mode]{pending_mode=mode;state.preferred_mode=mode;if(state.mode)Select(mode);else Set("PreferredMode",std::to_string(mode));},true);
      auto* body=gtk_box_new(GTK_ORIENTATION_VERTICAL,7);auto* icon=Icon(icons[i],("mode-"+std::to_string(mode)).c_str());gtk_image_set_pixel_size(GTK_IMAGE(icon),24);
      gtk_box_append(GTK_BOX(body),icon);auto* label=Label(titles[i],nullptr,false);gtk_label_set_wrap(GTK_LABEL(label),TRUE);gtk_label_set_justify(GTK_LABEL(label),GTK_JUSTIFY_CENTER);gtk_widget_set_halign(label,GTK_ALIGN_CENTER);gtk_box_append(GTK_BOX(body),label);
      gtk_button_set_child(GTK_BUTTON(b),body);gtk_widget_add_css_class(b,"tray-mode");modes[i]=b;if(i)gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b),GTK_TOGGLE_BUTTON(modes[0]));gtk_box_append(GTK_BOX(tiles),b);
    }gtk_box_append(GTK_BOX(card),tiles);
    gain_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_box_append(GTK_BOX(card),gain_box);
    if(state.advanced_optimized){const char* icons2[]={"audio-input-microphone-symbolic","multimedia-player-symbolic","audio-speakers-symbolic"};
      const char* tones[]={"accent-text","mode-3","mode-2"};const char* labels[]={L("Voice","الصوت البشري"),L("Music","الموسيقى"),L("Background","الخلفية")};
      for(int i=0;i<3;++i){gtk_box_append(GTK_BOX(gain_box),GainRow(labels[i],icons2[i],tones[i],&advanced_values[i]));advanced_scales[i]=Scale(i,advanced_values[i]);gtk_widget_add_css_class(advanced_scales[i],tones[i]);Name(advanced_scales[i],labels[i]);gtk_box_append(GTK_BOX(gain_box),advanced_scales[i]);}
    } else {gtk_box_append(GTK_BOX(gain_box),GainRow(L("Background","الخلفية"),"audio-speakers-symbolic","mode-2",&gain_value));gain_scale=Scale(-1,gain_value);Name(gain_scale,L("Background level","مستوى الخلفية"));gtk_box_append(GTK_BOX(gain_box),gain_scale);}
    auto* buffer_heading=IconLabel("io.github.altarafiya.Sukut-sliders-symbolic",L("Audio buffering","التخزين المؤقت للصوت"));gtk_widget_add_css_class(buffer_heading,"heading");gtk_box_append(GTK_BOX(card),buffer_heading);
    auto* levels=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_set_homogeneous(GTK_BOX(levels),TRUE);
    const char* level_icons[]={"io.github.altarafiya.Sukut-lightning-symbolic","io.github.altarafiya.Sukut-sliders-symbolic","io.github.altarafiya.Sukut-clock-symbolic"};
    const char* level_names[]={L("Lowest","الأقل"),L("Balanced","متوازن"),L("Maximum","الأكبر")};
    for(int i=0;i<3;++i){buffers[i]=Button(level_names[i],level_icons[i],[this,i]{state.buffering=i;ApplyChange("Buffering",std::to_string(i));},true);if(i)gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(buffers[i]),GTK_TOGGLE_BUTTON(buffers[0]));gtk_widget_add_css_class(buffers[i],"tray-buffer");gtk_box_append(GTK_BOX(levels),buffers[i]);}gtk_box_append(GTK_BOX(card),levels);
    auto* outputs=gtk_expander_new(nullptr);output_title=ChoiceHeading(L("Audio output","جهاز الصوت"),"audio-headphones-symbolic",&output_summary);gtk_expander_set_label_widget(GTK_EXPANDER(outputs),output_title);output_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_expander_set_child(GTK_EXPANDER(outputs),output_box);gtk_box_append(GTK_BOX(card),outputs);
    auto* sources=gtk_expander_new(nullptr);source_title=ChoiceHeading(L("Source app","التطبيق المصدر"),"application-x-executable-symbolic",&source_summary);gtk_expander_set_label_widget(GTK_EXPANDER(sources),source_title);source_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_expander_set_child(GTK_EXPANDER(sources),source_box);gtk_box_append(GTK_BOX(card),sources);
    for(auto* e:{outputs,sources})g_signal_connect(e,"notify::expanded",G_CALLBACK((+[](GObject* o,GParamSpec*,gpointer p){if(gtk_expander_get_expanded(GTK_EXPANDER(o)))static_cast<Popup*>(p)->CatalogNow(true);})),this);
    gtk_box_append(GTK_BOX(card),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    auto* footer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);auto* open=Button(L("Open Sukut","فتح سُكوت"),"window-new-symbolic",[this]{if(link)link->Send({"open"},[this](bool,const std::string&){Close();});});gtk_widget_set_hexpand(open,TRUE);gtk_box_append(GTK_BOX(footer),open);
    auto* exit=Button(L("Exit","خروج"),"application-exit-symbolic",[this]{if(link)link->Send({"exit"},[this](bool,const std::string&){Close();});});gtk_box_append(GTK_BOX(footer),exit);gtk_box_append(GTK_BOX(card),footer);
    Choices();Render();
  }
  void Choices() {
    if(!output_box)return;
    while(auto*c=gtk_widget_get_first_child(output_box))gtk_box_remove(GTK_BOX(output_box),c);
    while(auto*c=gtk_widget_get_first_child(source_box))gtk_box_remove(GTK_BOX(source_box),c);
    const std::string chosen=state.chosen_output.empty()?catalog.default_output:state.chosen_output;
    for(const auto& o:catalog.outputs){auto*b=Button(o.description.c_str(),o.name==chosen?"emblem-ok-symbolic":"audio-speakers-symbolic",[this,name=o.name]{choices_dirty=true;state.chosen_output=name;ApplyChange("Output",name);});gtk_box_append(GTK_BOX(output_box),b);}
    if(catalog.outputs.empty())gtk_box_append(GTK_BOX(output_box),Label(L("No output found","لم يُعثر على جهاز صوت"),"dim-label"));
    auto* all=Button(L("All system audio","صوت النظام بالكامل"),state.source_pid==0?"emblem-ok-symbolic":"computer-symbolic",[this]{choices_dirty=true;state.source_pid=0;ApplyChange("SourcePid","0");});gtk_box_append(GTK_BOX(source_box),all);
    for(const auto& a:catalog.apps){auto*b=Button(a.name.c_str(),a.pid==state.source_pid?"emblem-ok-symbolic":"application-x-executable-symbolic",[this,pid=a.pid]{choices_dirty=true;state.source_pid=pid;ApplyChange("SourcePid",std::to_string(pid));});gtk_box_append(GTK_BOX(source_box),b);}
  }
  void Verify() {
    g_assert_true(capture);g_assert_null(link.get());
    state.mode=2;state.advanced_optimized=true;Render();
    auto* range=GTK_RANGE(advanced_scales[2]);auto* a=gtk_range_get_adjustment(range);
    g_assert_cmpfloat(gtk_adjustment_get_lower(a),==,-20);g_assert_cmpfloat(gtk_adjustment_get_upper(a),==,10);
    g_assert_cmpfloat(gtk_adjustment_get_step_increment(a),==,1);
    dragging[3]=true;gtk_range_set_value(range,-14);state.advanced_background_db=-20;Render();
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(advanced_values[2])),==,"-14 dB");
    dragging[3]=false;Gain(2,-14);g_assert_cmpstr(captured_commands.back()[9].c_str(),==,"-14");
    g_signal_emit_by_name(modes[2],"clicked");g_assert_cmpstr(captured_commands.back()[1].c_str(),==,"3");
    g_signal_emit_by_name(buffers[2],"clicked");g_assert_cmpstr(captured_commands.back()[4].c_str(),==,"2");
    gboolean handled=FALSE;g_signal_emit_by_name(power,"state-set",FALSE,&handled);g_assert_cmpstr(captured_commands.back()[1].c_str(),==,"0");
    state.mode=0;g_signal_emit_by_name(modes[0],"clicked");g_assert_cmpstr(captured_commands.back()[0].c_str(),==,"settings");
    g_assert_cmpstr(captured_commands.back()[1].c_str(),==,"PreferredMode=1");
    g_print("tray_controls=PASS live_gain, range, mode, power, buffering, off_persistence; no audio host connected\n");
    state.preferred_mode=2;Render();
  }
  void Render() {
    if(!status)return;
    applying=true;
    gtk_label_set_text(GTK_LABEL(status),state.state==2?L("Attention · open Sukut","تنبيه · افتح سُكوت"):(state.busy||command)?L("Working…","جارٍ التنفيذ…"):state.mode==0?L("Off","متوقف"):state.standby?L("Standby","استعداد"):L("Active","نشط"));
    SetClass(status,"mode-2",state.state==2);
    gtk_switch_set_active(GTK_SWITCH(power),state.mode!=0);gtk_widget_set_sensitive(power,!command&&!state.busy);
    const int selected=pending_mode>0?pending_mode:state.mode?state.mode:state.preferred_mode;
    for(int i=0;i<3;++i){gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(modes[i]),selected==i+1);SetClass(modes[i],"selected",selected==i+1);gtk_widget_set_sensitive(modes[i],!command&&!state.busy);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buffers[i]),state.buffering==i);SetClass(buffers[i],"selected",state.buffering==i);gtk_widget_set_sensitive(buffers[i],!command&&!state.busy);}
    const std::string chosen=!state.output.empty()&&state.mode?state.output:!state.chosen_output.empty()?state.chosen_output:catalog.default_output;
    std::string output=L("System output","جهاز النظام"),source=L("All system audio","صوت النظام بالكامل");
    for(const auto& o:catalog.outputs)if(o.name==chosen)output=o.description;
    if(state.source_pid){source=L("Selected app (unavailable)","التطبيق المحدد (غير متاح)");for(const auto& a:catalog.apps)if(a.pid==state.source_pid)source=a.name;}
    gtk_label_set_text(GTK_LABEL(output_summary),output.c_str());gtk_label_set_text(GTK_LABEL(source_summary),source.c_str());
    for(auto* control:{gain_box,output_box,source_box})gtk_widget_set_sensitive(control,!command&&!state.busy);
    gtk_widget_set_visible(gain_box,selected==2);
    if(gain_scale){if(!dragging[0])gtk_range_set_value(GTK_RANGE(gain_scale),state.background_db);gtk_label_set_text(GTK_LABEL(gain_value),Db(static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(gain_scale))))).c_str());}
    const int gains[]={state.advanced_voice_db,state.advanced_music_db,state.advanced_background_db};
    for(int i=0;i<3;++i)if(advanced_scales[i]){if(!dragging[i+1])gtk_range_set_value(GTK_RANGE(advanced_scales[i]),gains[i]);gtk_label_set_text(GTK_LABEL(advanced_values[i]),Db(static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(advanced_scales[i]))))).c_str());}
    applying=false;
  }
};
void PopupStyle(){
  auto* css=gtk_css_provider_new();gtk_css_provider_load_from_string(css,R"css(
window.tray-overlay { background: transparent; }
.tray-flyout { background: @window_bg_color; border: 1px solid alpha(@window_fg_color,.14); border-radius: 20px; padding: 16px;  }
.tray-mode { padding: 12px 6px; border: 1px solid alpha(@window_fg_color,.12); border-radius: 14px; background: alpha(@window_fg_color,.035); }
.tray-mode.selected { border: 2px solid @accent_color; padding: 11px 5px; background: alpha(@accent_color,.13); }
.tray-buffer { padding: 8px 4px; font-size: 12px; border-radius: 10px; }
.tray-buffer.selected { background: alpha(@accent_color,.14); color: @accent_color; }
.tray-flyout expander { padding: 8px 4px; }
.tray-flyout scale { margin: 0; padding: 4px 0 8px; }
)css");gtk_style_context_add_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+1);g_object_unref(css);
}
bool Save(GtkWidget* widget,const std::filesystem::path& file){
  const int width=gtk_widget_get_width(widget),height=gtk_widget_get_height(widget);g_print("snapshot %dx%d mapped=%d\n",width,height,gtk_widget_get_mapped(widget));auto*s=gtk_snapshot_new();auto* child=GTK_IS_WINDOW(widget)?gtk_window_get_child(GTK_WINDOW(widget)):widget;auto* parent=gtk_widget_get_parent(child);if(parent)gtk_widget_snapshot_child(parent,child,s);auto*n=gtk_snapshot_free_to_node(s);if(!n){g_printerr("tray snapshot has no node\n");return false;}
  auto*r=gsk_cairo_renderer_new();if(!gsk_renderer_realize_for_display(r,gdk_display_get_default(),nullptr)){gsk_render_node_unref(n);return false;}
  const graphene_rect_t rect=GRAPHENE_RECT_INIT(0,0,static_cast<float>(width),static_cast<float>(height));auto*t=gsk_renderer_render_texture(r,n,&rect);const bool ok=gdk_texture_save_to_png(t,file.c_str());g_object_unref(t);gsk_renderer_unrealize(r);g_object_unref(r);gsk_render_node_unref(n);return ok;
}
}

bool TrayPopupSupported() {
#ifdef SUKUT_HAS_LAYER_SHELL
  gtk_init();return gtk_layer_is_supported();
#else
  return false;
#endif
}
int RunTrayPopup(const std::string& host_command,int x,int y,const std::string& capture_directory,const std::string& snapshot,bool exercise){
  Popup popup;popup.x=x;popup.y=y;popup.capture=!capture_directory.empty();
  popup.app=GTK_APPLICATION(adw_application_new("io.github.altarafiya.Sukut.TrayPopup",G_APPLICATION_NON_UNIQUE));
  struct Launch{Popup* p;std::string host,capture,snapshot;bool exercise;};Launch launch{&popup,host_command,capture_directory,snapshot,exercise};
  g_signal_connect(popup.app,"activate",G_CALLBACK((+[](GtkApplication* app,gpointer data){
    auto*l=static_cast<Launch*>(data);auto*p=l->p;InstallStyle();PopupStyle();g_object_set(gtk_settings_get_default(),"gtk-enable-animations",FALSE,nullptr);
    if(!p->capture){p->link=std::make_unique<HostLink>();std::string error;if(!p->link->Connect(l->host,error,true,l->snapshot)){g_printerr("tray: %s\n",error.c_str());p->Close();return;}p->state=HostState::From(Json::Parse(p->link->InitialState()));p->link->OnLost([p]{p->Close();});}
    else {p->state.theme=p->x==1?"light":"dark";p->state.language=p->x==1?"ar":"en";p->state.preferred_mode=2;p->state.background_db=-10;p->state.advanced_optimized=p->x>=2;p->state.advanced_music_db=-20;p->state.advanced_background_db=-14;p->catalog.outputs={{"speakers","Speakers"},{"headphones","Headphones"}};p->catalog.apps={{100,"Firefox"},{200,"Videos"}};p->catalog.default_output="speakers";}
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(),p->state.theme=="light"?ADW_COLOR_SCHEME_FORCE_LIGHT:p->state.theme=="dark"?ADW_COLOR_SCHEME_FORCE_DARK:ADW_COLOR_SCHEME_DEFAULT);
    p->window=gtk_application_window_new(app);gtk_window_set_title(GTK_WINDOW(p->window),"Sukut");gtk_window_set_decorated(GTK_WINDOW(p->window),FALSE);gtk_widget_add_css_class(p->window,"sukut");gtk_widget_add_css_class(p->window,"tray-overlay");
    p->Build();if(p->capture && p->x==3)p->Verify();auto* scroll=gtk_scrolled_window_new();p->scroller=scroll;
    // Keep placement in physical left-to-right coordinates; only the card mirrors.
    gtk_widget_set_direction(scroll,GTK_TEXT_DIR_LTR);gtk_widget_set_direction(p->card,p->state.Arabic()?GTK_TEXT_DIR_RTL:GTK_TEXT_DIR_LTR);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),p->card);gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),TRUE);gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),720);
    if(p->capture){gtk_window_set_default_size(GTK_WINDOW(p->window),372,-1);auto* outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);for(auto edge:{gtk_widget_set_margin_top,gtk_widget_set_margin_bottom,gtk_widget_set_margin_start,gtk_widget_set_margin_end})edge(scroll,16);gtk_box_append(GTK_BOX(outer),scroll);gtk_window_set_child(GTK_WINDOW(p->window),outer);}
    else {
#ifdef SUKUT_HAS_LAYER_SHELL
      if(!gtk_layer_is_supported()){p->unsupported=true;g_printerr("tray: compositor does not support layer shell\n");g_application_quit(G_APPLICATION(app));return;}
      auto* monitors=gdk_display_get_monitors(gdk_display_get_default());GdkMonitor* monitor=nullptr;GdkRectangle bounds{0,0,1920,1080};
      for(guint i=0;i<g_list_model_get_n_items(monitors);++i){auto*m=GDK_MONITOR(g_list_model_get_item(monitors,i));GdkRectangle b;gdk_monitor_get_geometry(m,&b);if(!monitor){monitor=m;bounds=b;}else if(p->x>=b.x&&p->x<b.x+b.width&&p->y>=b.y&&p->y<b.y+b.height){g_object_unref(monitor);monitor=m;bounds=b;break;}else g_object_unref(m);}
      // KWin maps utility to a tool surface, avoiding normal-window opening effects.
      gtk_layer_init_for_window(GTK_WINDOW(p->window));gtk_layer_set_namespace(GTK_WINDOW(p->window),"utility");gtk_layer_set_layer(GTK_WINDOW(p->window),GTK_LAYER_SHELL_LAYER_OVERLAY);gtk_layer_set_keyboard_mode(GTK_WINDOW(p->window),GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);gtk_layer_set_exclusive_zone(GTK_WINDOW(p->window),0);
      if(monitor){gtk_layer_set_monitor(GTK_WINDOW(p->window),monitor);g_object_unref(monitor);}for(int edge=0;edge<GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER;++edge)gtk_layer_set_anchor(GTK_WINDOW(p->window),static_cast<GtkLayerShellEdge>(edge),TRUE);
      p->canvas=gtk_overlay_new();gtk_widget_set_direction(p->canvas,GTK_TEXT_DIR_LTR);gtk_overlay_set_child(GTK_OVERLAY(p->canvas),gtk_box_new(GTK_ORIENTATION_VERTICAL,0));gtk_overlay_add_overlay(GTK_OVERLAY(p->canvas),scroll);
      const bool top=p->y-bounds.y<bounds.height/2;gtk_widget_set_halign(scroll,GTK_ALIGN_START);gtk_widget_set_valign(scroll,top?GTK_ALIGN_START:GTK_ALIGN_END);
      p->monitor_bounds=bounds;
      gtk_widget_set_margin_start(scroll,TrayLeft(p->x-bounds.x,bounds.width,340));
      gtk_widget_set_margin_top(scroll,8);gtk_widget_set_margin_bottom(scroll,8);
      gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),std::max(240,bounds.height-64));gtk_window_set_child(GTK_WINDOW(p->window),p->canvas);
      auto* outside=gtk_gesture_click_new();gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(outside),GTK_PHASE_CAPTURE);
      g_signal_connect(outside,"pressed",G_CALLBACK((+[](GtkGestureClick* g,int,double px,double py,gpointer data){auto*p=static_cast<Popup*>(data);graphene_rect_t b;const graphene_point_t point=GRAPHENE_POINT_INIT(static_cast<float>(px),static_cast<float>(py));if(gtk_widget_compute_bounds(p->card,p->canvas,&b)&&!graphene_rect_contains_point(&b,&point)){gtk_gesture_set_state(GTK_GESTURE(g),GTK_EVENT_SEQUENCE_CLAIMED);p->Close();}})),p);gtk_widget_add_controller(p->canvas,GTK_EVENT_CONTROLLER(outside));
#else
      gtk_window_set_child(GTK_WINDOW(p->window),scroll);
#endif
    }
    auto* key=gtk_event_controller_key_new();g_signal_connect(key,"key-pressed",G_CALLBACK((+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer data)->gboolean{if(key==GDK_KEY_Escape){static_cast<Popup*>(data)->Close();return TRUE;}return FALSE;})),p);gtk_widget_add_controller(p->window,key);
    if(!p->capture) {
      // Disabling the clicked widget moves logical focus. Only a native surface
      // focus departure dismisses the menu; internal control changes must not.
      auto* native_focus=gtk_event_controller_legacy_new();
      g_signal_connect(native_focus,"event",G_CALLBACK((+[](GtkEventControllerLegacy*,GdkEvent* event,gpointer data)->gboolean {
        auto* p=static_cast<Popup*>(data);
        if(gdk_event_get_event_type(event)==GDK_FOCUS_CHANGE) {
          if(gdk_focus_event_get_in(event))p->native_focused=true;
          else if(p->native_focused)p->Close();
        }
        return FALSE;
      })),p);
      gtk_widget_add_controller(p->window,native_focus);
    }
    gtk_window_present(GTK_WINDOW(p->window));
    if(!p->capture) {
      gtk_widget_grab_focus(p->power);
      auto* surface=gtk_native_get_surface(GTK_NATIVE(p->window));
      g_signal_connect(surface,"layout",G_CALLBACK((+[](GdkSurface*,int width,int height,gpointer data){
        auto* p=static_cast<Popup*>(data);
        int minimum=0;
        // The scroller uses the card's minimum width (long labels can wrap).
        gtk_widget_measure(p->card,GTK_ORIENTATION_HORIZONTAL,-1,&minimum,nullptr,nullptr,nullptr);
        const int menu_width=std::max(340,minimum);
        gtk_widget_set_margin_start(p->scroller,TrayLeft(p->x-p->monitor_bounds.x,width,menu_width));
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(p->scroller),std::max(120,height-16));
      })),p);
      if(l->exercise) {
        auto* clock=gtk_widget_get_frame_clock(p->window);
        g_signal_connect_after(clock,"after-paint",G_CALLBACK((+[](GdkFrameClock*,gpointer data){
          auto* p=static_cast<Popup*>(data);if(p->first_frame)return;p->first_frame=true;
          g_print("tray_first_frame renderer=%s\n",G_OBJECT_TYPE_NAME(gtk_native_get_renderer(GTK_NATIVE(p->window))));fflush(stdout);
        })),p);
        g_timeout_add(400,+[](gpointer data)->gboolean {
          auto* p=static_cast<Popup*>(data);gtk_widget_grab_focus(p->modes[2]);
          g_signal_emit_by_name(p->modes[2],"clicked");
          g_print("tray_exercise=mode_clicked\n");fflush(stdout);
          g_timeout_add(150,+[](gpointer data)->gboolean {
            auto* p=static_cast<Popup*>(data);graphene_rect_t rect;
            if(gtk_widget_compute_bounds(p->card,p->canvas,&rect))g_print("tray_bounds %.0f %.0f %.0f %.0f %d %d\n",rect.origin.x,rect.origin.y,rect.size.width,rect.size.height,gtk_widget_get_width(p->canvas),gtk_widget_get_height(p->canvas));
            fflush(stdout);return G_SOURCE_REMOVE;
          },p);
          return G_SOURCE_REMOVE;
        },p);
      }
    }
    if(p->capture){std::filesystem::create_directories(l->capture);g_timeout_add(500,+[](gpointer data)->gboolean{auto*l=static_cast<Launch*>(data);const auto path=std::filesystem::path(l->capture)/("tray-"+std::to_string(l->p->x)+".png");const bool ok=Save(l->p->window,path);g_print("tray_capture=%s %s\n",ok?"PASS":"FAIL",path.c_str());l->p->Close();return G_SOURCE_REMOVE;},l);}
    else {p->Refresh();p->CatalogNow();p->poll=g_timeout_add(2000,+[](gpointer d)->gboolean{static_cast<Popup*>(d)->Refresh();return G_SOURCE_CONTINUE;},p);}
  })),&launch);
  char name[]="sukut-tray";char* args[]={name,nullptr};const int result=g_application_run(G_APPLICATION(popup.app),1,args);popup.link.reset();g_object_unref(popup.app);return popup.unsupported?77:result;
}
}
