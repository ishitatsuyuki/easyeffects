/*
 *  Copyright © 2017-2022 Wellington Wallace
 *
 *  This file is part of EasyEffects.
 *
 *  EasyEffects is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  EasyEffects is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with EasyEffects.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "plugins_box.hpp"

namespace ui::plugins_box {

using namespace std::string_literals;

auto constexpr log_tag = "plugins_box: ";

struct _PluginsBox {
  GtkBox parent_instance;

  GtkMenuButton* menubutton_plugins;

  GtkListView* listview;

  AdwViewStack* stack;

  ui::plugins_menu::PluginsMenu* plugins_menu;

  app::Application* application;

  bool schedule_signal_idle;

  PipelineType pipeline_type;

  GtkStringList* string_list;

  GSettings* settings;

  std::vector<sigc::connection> connections;

  std::vector<gulong> gconnections;
};

G_DEFINE_TYPE(PluginsBox, plugins_box, GTK_TYPE_BOX)

template <PipelineType pipeline_type>
void add_plugins_to_stack(PluginsBox* self) {
  std::string schema_path;
  EffectsBase* effects_base;

  if constexpr (pipeline_type == PipelineType::input) {
    schema_path = "/com/github/wwmm/easyeffects/streaminputs/";

    effects_base = self->application->sie;
  } else if constexpr (pipeline_type == PipelineType::output) {
    schema_path = "/com/github/wwmm/easyeffects/streamoutputs/";

    effects_base = self->application->soe;
  }

  std::replace(schema_path.begin(), schema_path.end(), '.', '/');

  // removing plugins that are not in the list

  std::vector<AdwViewStackPage*> pages_to_remove;

  auto pages = G_LIST_MODEL(adw_view_stack_get_pages(self->stack));

  for (guint n = 0; n < g_list_model_get_n_items(pages); n++) {
    auto page = ADW_VIEW_STACK_PAGE(g_list_model_get_item(pages, n));
    auto page_name = adw_view_stack_page_get_name(page);

    auto list = util::gchar_array_to_vector(g_settings_get_strv(self->settings, "plugins"));

    if (std::ranges::find(list, page_name) == list.end()) {
      pages_to_remove.push_back(page);
    }
  }

  for (auto page : pages_to_remove) {
    adw_view_stack_remove(self->stack, GTK_WIDGET(page));
  }

  // Adding to the stack the plugins in the list that are not there yet

  for (const auto& name : util::gchar_array_to_vector(g_settings_get_strv(self->settings, "plugins"))) {
    auto found = false;

    auto pages = G_LIST_MODEL(adw_view_stack_get_pages(self->stack));

    for (guint n = 0; n < g_list_model_get_n_items(pages); n++) {
      if (name == adw_view_stack_page_get_name(ADW_VIEW_STACK_PAGE(g_list_model_get_item(pages, n)))) {
        found = true;

        break;
      }
    }

    if (found) {
      continue;
    }

    if (name == plugin_name::autogain) {
      auto* box = ui::autogain_box::create();

      ui::autogain_box::setup(box, effects_base->autogain, schema_path + "autogain/");

      adw_view_stack_add_named(self->stack, GTK_WIDGET(box), plugin_name::autogain);
    }
  }
}

void setup_listview(PluginsBox* self) {
  if (const auto list = util::gchar_array_to_vector(g_settings_get_strv(self->settings, "plugins")); !list.empty()) {
    for (const auto& name : list) {
      gtk_string_list_append(self->string_list, name.c_str());
    }

    // showing the first plugin in the list by default

    const auto* selected_name = gtk_string_list_get_string(self->string_list, 0);

    auto pages = G_LIST_MODEL(adw_view_stack_get_pages(self->stack));

    for (guint n = 0; n < g_list_model_get_n_items(pages); n++) {
      auto page = ADW_VIEW_STACK_PAGE(g_list_model_get_item(pages, n));

      if (selected_name == adw_view_stack_page_get_name(page)) {
        adw_view_stack_set_visible_child(self->stack, GTK_WIDGET(page));

        break;
      }
    }
  }

  self->gconnections.push_back(g_signal_connect(
      self->settings, "changed::plugins", G_CALLBACK(+[](GSettings* settings, char* key, PluginsBox* self) {
        if (const auto glist = g_settings_get_strv(settings, key); glist != nullptr) {
          gtk_string_list_splice(self->string_list, 0, g_list_model_get_n_items(G_LIST_MODEL(self->string_list)),
                                 glist);

          const auto list = util::gchar_array_to_vector(glist);

          if (!list.empty()) {
            auto* visible_child = adw_view_stack_get_visible_child(self->stack);

            if (visible_child == nullptr) {
              return;
            }

            auto* visible_page_name = adw_view_stack_page_get_name(ADW_VIEW_STACK_PAGE(visible_child));

            if (std::ranges::find(list, visible_page_name) == list.end()) {
              gtk_selection_model_select_item(gtk_list_view_get_model(self->listview), 0, 1);

              auto pages = G_LIST_MODEL(adw_view_stack_get_pages(self->stack));

              for (guint n = 0; n < g_list_model_get_n_items(pages); n++) {
                auto page = ADW_VIEW_STACK_PAGE(g_list_model_get_item(pages, n));

                if (list[0] == adw_view_stack_page_get_name(page)) {
                  adw_view_stack_set_visible_child(self->stack, GTK_WIDGET(page));

                  break;
                }
              }
            } else {
              for (size_t m = 0U; m < list.size(); m++) {
                if (list[m] == visible_page_name) {
                  gtk_selection_model_select_item(gtk_list_view_get_model(self->listview), m, 1);

                  break;
                }
              }
            }
          }
        }
      }),
      self));

  auto* factory = gtk_signal_list_item_factory_new();

  // setting the factory callbacks

  g_signal_connect(
      factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory* factory, GtkListItem* item, PluginsBox* self) {
        auto builder = gtk_builder_new_from_resource("/com/github/wwmm/easyeffects/ui/plugin_row.ui");

        auto* top_box = gtk_builder_get_object(builder, "top_box");
        auto* plugin_icon = gtk_builder_get_object(builder, "plugin_icon");
        auto* remove = gtk_builder_get_object(builder, "remove");
        auto* drag_handle = gtk_builder_get_object(builder, "drag_handle");

        g_object_set_data(G_OBJECT(item), "top_box", top_box);
        g_object_set_data(G_OBJECT(item), "plugin_icon", plugin_icon);
        g_object_set_data(G_OBJECT(item), "name", gtk_builder_get_object(builder, "name"));
        g_object_set_data(G_OBJECT(item), "remove", remove);
        g_object_set_data(G_OBJECT(item), "drag_handle", drag_handle);

        gtk_list_item_set_activatable(item, 0);
        gtk_list_item_set_child(item, GTK_WIDGET(top_box));

        g_object_unref(builder);

        // showing/hiding icons based on wether the mouse is over the plugin row

        auto* controller = gtk_event_controller_motion_new();

        g_object_set_data(G_OBJECT(controller), "remove", remove);
        g_object_set_data(G_OBJECT(controller), "drag_handle", drag_handle);

        g_signal_connect(controller, "enter",
                         G_CALLBACK(+[](GtkEventControllerMotion* controller, gdouble x, gdouble y, PluginsBox* self) {
                           gtk_widget_set_opacity(GTK_WIDGET(g_object_get_data(G_OBJECT(controller), "remove")), 1.0);
                           gtk_widget_set_opacity(GTK_WIDGET(g_object_get_data(G_OBJECT(controller), "drag_handle")),
                                                  1.0);
                         }),
                         self);

        g_signal_connect(controller, "leave", G_CALLBACK(+[](GtkEventControllerMotion* controller, PluginsBox* self) {
                           gtk_widget_set_opacity(GTK_WIDGET(g_object_get_data(G_OBJECT(controller), "remove")), 0.0);
                           gtk_widget_set_opacity(GTK_WIDGET(g_object_get_data(G_OBJECT(controller), "drag_handle")),
                                                  0.0);
                         }),
                         self);

        gtk_widget_add_controller(GTK_WIDGET(top_box), controller);

        // Configuring row drag and drop

        auto* drag_source = gtk_drag_source_new();

        gtk_drag_source_set_actions(drag_source, GDK_ACTION_MOVE);

        g_object_set_data(G_OBJECT(drag_source), "top_box", top_box);

        g_signal_connect(
            drag_source, "prepare", G_CALLBACK(+[](GtkDragSource* source, double x, double y, PluginsBox* self) {
              auto* top_box = static_cast<GtkBox*>(g_object_get_data(G_OBJECT(source), "top_box"));

              auto* paintable = gtk_widget_paintable_new(GTK_WIDGET(top_box));

              gtk_drag_source_set_icon(source, paintable, 0, 0);

              if (auto* string_object = GTK_STRING_OBJECT(g_object_get_data(G_OBJECT(top_box), "string-object"));
                  string_object != nullptr) {
                auto* plugin_name = gtk_string_object_get_string(string_object);

                return gdk_content_provider_new_typed(G_TYPE_STRING, plugin_name);
              }

              return gdk_content_provider_new_typed(G_TYPE_STRING, "");
            }),
            self);

        auto* drop_target = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_MOVE);

        g_object_set_data(G_OBJECT(drop_target), "top_box", top_box);

        g_signal_connect(
            drop_target, "drop",
            G_CALLBACK(+[](GtkDropTarget* target, const GValue* value, double x, double y, PluginsBox* self) {
              if (!G_VALUE_HOLDS(value, G_TYPE_STRING)) {
                return false;
              }

              auto* top_box = static_cast<GtkBox*>(g_object_get_data(G_OBJECT(target), "top_box"));

              if (auto* string_object = GTK_STRING_OBJECT(g_object_get_data(G_OBJECT(top_box), "string-object"));
                  string_object != nullptr) {
                auto* dst = gtk_string_object_get_string(string_object);

                auto* src = g_value_get_string(value);

                if (g_strcmp0(src, dst) != 0) {
                  auto list = util::gchar_array_to_vector(g_settings_get_strv(self->settings, "plugins"));

                  auto iter_src = std::ranges::find(list, src);
                  auto iter_dst = std::ranges::find(list, dst);

                  auto insert_after = (iter_src - list.begin() < iter_dst - list.begin()) ? true : false;

                  list.erase(iter_src);

                  iter_dst = std::ranges::find(list, dst);

                  list.insert(((insert_after) ? (iter_dst + 1) : iter_dst), src);

                  g_settings_set_strv(self->settings, "plugins", util::make_gchar_pointer_vector(list).data());

                  return true;
                }

                return false;
              }

              return false;
            }),
            self);

        gtk_widget_add_controller(GTK_WIDGET(drag_handle), GTK_EVENT_CONTROLLER(drag_source));
        gtk_widget_add_controller(GTK_WIDGET(top_box), GTK_EVENT_CONTROLLER(drop_target));

        g_signal_connect(
            remove, "clicked", G_CALLBACK(+[](GtkButton* btn, PluginsBox* self) {
              if (auto* string_object = GTK_STRING_OBJECT(g_object_get_data(G_OBJECT(btn), "string-object"));
                  string_object != nullptr) {
                auto* name = gtk_string_object_get_string(string_object);

                auto list = util::gchar_array_to_vector(g_settings_get_strv(self->settings, "plugins"));

                list.erase(std::remove_if(list.begin(), list.end(),
                                          [=](const auto& plugin_name) { return plugin_name == name; }),
                           list.end());

                g_settings_set_strv(self->settings, "plugins", util::make_gchar_pointer_vector(list).data());
              }
            }),
            self);
      }),
      self);

  g_signal_connect(factory, "bind",
                   G_CALLBACK(+[](GtkSignalListItemFactory* factory, GtkListItem* item, PluginsBox* self) {
                     auto* top_box = static_cast<GtkBox*>(g_object_get_data(G_OBJECT(item), "top_box"));
                     auto* label = static_cast<GtkLabel*>(g_object_get_data(G_OBJECT(item), "name"));
                     auto* remove = static_cast<GtkButton*>(g_object_get_data(G_OBJECT(item), "remove"));
                     auto* plugin_icon = static_cast<GtkImage*>(g_object_get_data(G_OBJECT(item), "plugin_icon"));

                     auto* child_item = gtk_list_item_get_item(item);
                     auto* string_object = GTK_STRING_OBJECT(child_item);

                     g_object_set_data(G_OBJECT(top_box), "string-object", string_object);
                     g_object_set_data(G_OBJECT(remove), "string-object", string_object);

                     auto* name = gtk_string_object_get_string(GTK_STRING_OBJECT(child_item));

                     gtk_label_set_text(label, plugin_name::translated[name].c_str());

                     gtk_accessible_update_property(GTK_ACCESSIBLE(remove), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                                    (_("Remove") + " "s + plugin_name::translated[name]).c_str(), -1);

                     const auto list = util::gchar_array_to_vector(g_settings_get_strv(self->settings, "plugins"));

                     if (const auto iter_name = std::ranges::find(list, name);
                         (iter_name == list.begin() && iter_name != list.end() - 2) || iter_name == list.end() - 1) {
                       gtk_image_set_from_icon_name(plugin_icon, "ee-square-symbolic");
                     } else {
                       gtk_image_set_from_icon_name(plugin_icon, "ee-arrow-down-symbolic");
                     }
                   }),
                   self);

  gtk_list_view_set_factory(self->listview, factory);

  g_object_unref(factory);

  // setting the listview model

  auto* selection = gtk_single_selection_new(G_LIST_MODEL(self->string_list));
  // auto* selection = gtk_single_selection_new(G_LIST_MODEL(adw_view_stack_get_pages(self->stack)));

  gtk_list_view_set_model(self->listview, GTK_SELECTION_MODEL(selection));

  g_object_unref(selection);
}

void setup(PluginsBox* self, app::Application* application, PipelineType pipeline_type) {
  self->application = application;
  self->pipeline_type = pipeline_type;

  switch (pipeline_type) {
    case PipelineType::input: {
      self->settings = g_settings_new("com.github.wwmm.easyeffects.streaminputs");

      add_plugins_to_stack<PipelineType::input>(self);

      break;
    }
    case PipelineType::output: {
      self->settings = g_settings_new("com.github.wwmm.easyeffects.streamoutputs");

      add_plugins_to_stack<PipelineType::output>(self);

      break;
    }
  }

  ui::plugins_menu::setup(self->plugins_menu, application, pipeline_type);

  setup_listview(self);
}

void realize(GtkWidget* widget) {
  auto* self = EE_PLUGINS_BOX(widget);

  self->schedule_signal_idle = true;

  GTK_WIDGET_CLASS(plugins_box_parent_class)->realize(widget);
}

void unroot(GtkWidget* widget) {
  auto* self = EE_PLUGINS_BOX(widget);

  self->schedule_signal_idle = false;

  GTK_WIDGET_CLASS(plugins_box_parent_class)->unroot(widget);
}

void dispose(GObject* object) {
  auto* self = EE_PLUGINS_BOX(object);

  for (auto& c : self->connections) {
    c.disconnect();
  }

  for (auto& handler_id : self->gconnections) {
    g_signal_handler_disconnect(self->settings, handler_id);
  }

  self->connections.clear();
  self->gconnections.clear();

  g_object_unref(self->settings);

  util::debug(log_tag + "disposed"s);

  G_OBJECT_CLASS(plugins_box_parent_class)->dispose(object);
}

void plugins_box_class_init(PluginsBoxClass* klass) {
  auto* object_class = G_OBJECT_CLASS(klass);
  auto* widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = dispose;

  widget_class->realize = realize;
  widget_class->unroot = unroot;

  gtk_widget_class_set_template_from_resource(widget_class, "/com/github/wwmm/easyeffects/ui/plugins_box.ui");

  gtk_widget_class_bind_template_child(widget_class, PluginsBox, menubutton_plugins);
  gtk_widget_class_bind_template_child(widget_class, PluginsBox, listview);
  gtk_widget_class_bind_template_child(widget_class, PluginsBox, stack);
}

void plugins_box_init(PluginsBox* self) {
  gtk_widget_init_template(GTK_WIDGET(self));

  self->schedule_signal_idle = false;

  self->string_list = gtk_string_list_new(nullptr);

  self->plugins_menu = ui::plugins_menu::create();

  gtk_menu_button_set_popover(self->menubutton_plugins, GTK_WIDGET(self->plugins_menu));
}

auto create() -> PluginsBox* {
  return static_cast<PluginsBox*>(g_object_new(EE_TYPE_PLUGINS_BOX, nullptr));
}

}  // namespace ui::plugins_box