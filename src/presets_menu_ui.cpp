#include "presets_menu_ui.hpp"
#include <glibmm/i18n.h>
#include <gtkmm/applicationwindow.h>
#include "util.hpp"

PresetsMenuUi::PresetsMenuUi(BaseObjectType* cobject,
                             const Glib::RefPtr<Gtk::Builder>& builder,
                             const Glib::RefPtr<Gio::Settings>& refSettings,
                             Application* application)
    : Gtk::Grid(cobject), settings(refSettings), app(application) {
  // loading glade widgets

  builder->get_widget("output_listbox", output_listbox);
  builder->get_widget("output_scrolled_window", output_scrolled_window);
  builder->get_widget("output_name", output_name);
  builder->get_widget("add_output", add_output);
  builder->get_widget("import_output", import_output);
  builder->get_widget("input_listbox", input_listbox);
  builder->get_widget("input_scrolled_window", input_scrolled_window);
  builder->get_widget("input_name", input_name);
  builder->get_widget("add_input", add_input);
  builder->get_widget("import_input", import_input);

  // signals connection

  output_listbox->set_sort_func(
      sigc::mem_fun(*this, &PresetsMenuUi::on_listbox_sort));

  input_listbox->set_sort_func(
      sigc::mem_fun(*this, &PresetsMenuUi::on_listbox_sort));

  add_output->signal_clicked().connect(
      [=]() { create_preset(PresetType::output); });

  add_input->signal_clicked().connect(
      [=]() { create_preset(PresetType::input); });

  import_output->signal_clicked().connect(
      [=]() { import_preset(PresetType::output); });

  import_input->signal_clicked().connect(
      [=]() { import_preset(PresetType::input); });
}

PresetsMenuUi::~PresetsMenuUi() {
  for (auto c : connections) {
    c.disconnect();
  }

  util::debug(log_tag + "destroyed");
}

PresetsMenuUi* PresetsMenuUi::add_to_popover(Gtk::Popover* popover,
                                             Application* app) {
  auto builder = Gtk::Builder::create_from_resource(
      "/com/github/wwmm/pulseeffects/ui/presets_menu.glade");

  auto settings = Gio::Settings::create("com.github.wwmm.pulseeffects");

  PresetsMenuUi* ui;

  builder->get_widget_derived("widgets_grid", ui, settings, app);

  popover->add(*ui);

  return ui;
}

void PresetsMenuUi::create_preset(PresetType preset_type) {
  std::string name;

  if (preset_type == PresetType::output) {
    name = output_name->get_text();
  } else {
    name = input_name->get_text();
  }

  if (!name.empty()) {
    std::string illegalChars = "\\/\0";

    for (auto it = name.begin(); it < name.end(); ++it) {
      bool found = illegalChars.find(*it) != std::string::npos;

      if (found) {
        if (preset_type == PresetType::output) {
          output_name->set_text("");
        } else {
          input_name->set_text("");
        }

        return;
      }
    }

    if (preset_type == PresetType::output) {
      output_name->set_text("");
    } else {
      // app->presets_manager->add(name);
      input_name->set_text("");
    }

    app->presets_manager->add(preset_type, name);

    populate_listbox(preset_type);
  }
}

void PresetsMenuUi::import_preset(PresetType preset_type) {
  // gtkmm 3.22 does not have FileChooseNative so we have to use C api :-(

  gint res;

  auto main_window = gtk_widget_get_toplevel((GtkWidget*)this->gobj());

  auto dialog = gtk_file_chooser_native_new(
      _("Import Presets"), (GtkWindow*)main_window,
      GTK_FILE_CHOOSER_ACTION_OPEN, _("Open"), _("Cancel"));

  auto filter = gtk_file_filter_new();

  gtk_file_filter_set_name(filter, _("Presets"));
  gtk_file_filter_add_pattern(filter, "*.json");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog));

  if (res == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

    auto file_list = gtk_file_chooser_get_filenames(chooser);

    if (preset_type == PresetType::input) {
      g_slist_foreach(file_list,
                      [](auto data, auto user_data) {
                        auto aui = static_cast<PresetsMenuUi*>(user_data);

                        auto file_path = static_cast<char*>(data);

                        aui->app->presets_manager->import(PresetType::input,
                                                          file_path);
                      },
                      this);
    }

    if (preset_type == PresetType::output) {
      g_slist_foreach(file_list,
                      [](auto data, auto user_data) {
                        auto aui = static_cast<PresetsMenuUi*>(user_data);

                        auto file_path = static_cast<char*>(data);

                        aui->app->presets_manager->import(PresetType::output,
                                                          file_path);
                      },
                      this);
    }

    g_slist_free(file_list);
  }

  g_object_unref(dialog);

  populate_listbox(preset_type);
}

int PresetsMenuUi::on_listbox_sort(Gtk::ListBoxRow* row1,
                                   Gtk::ListBoxRow* row2) {
  auto name1 = row1->get_name();
  auto name2 = row2->get_name();

  std::vector<std::string> names = {name1, name2};

  std::sort(names.begin(), names.end());

  if (name1 == names[0]) {
    return -1;
  } else if (name2 == names[0]) {
    return 1;
  } else {
    return 0;
  }
}

void PresetsMenuUi::on_presets_menu_button_clicked() {
  Gtk::ApplicationWindow* parent =
      dynamic_cast<Gtk::ApplicationWindow*>(this->get_toplevel());

  int height = 0.7 * parent->get_allocated_height();

  output_scrolled_window->set_max_content_height(height);

  populate_listbox(PresetType::input);
  populate_listbox(PresetType::output);
}

void PresetsMenuUi::populate_listbox(PresetType preset_type) {
  Gtk::ListBox* listbox;

  if (preset_type == PresetType::output) {
    listbox = output_listbox;
  } else {
    listbox = input_listbox;
  }

  auto children = listbox->get_children();

  for (auto c : children) {
    listbox->remove(*c);
  }

  bool reset_menu_button_label = true;

  auto names = app->presets_manager->get_names();

  for (auto name : names) {
    auto b = Gtk::Builder::create_from_resource(
        "/com/github/wwmm/pulseeffects/ui/preset_row.glade");

    Gtk::ListBoxRow* row;
    Gtk::Button *apply_btn, *save_btn, *remove_btn;
    Gtk::Label* label;

    b->get_widget("preset_row", row);
    b->get_widget("apply", apply_btn);
    b->get_widget("save", save_btn);
    b->get_widget("remove", remove_btn);
    b->get_widget("name", label);

    row->set_name(name);

    label->set_text(name);

    connections.push_back(apply_btn->signal_clicked().connect([=]() {
      settings->set_string("last-used-preset", row->get_name());

      app->presets_manager->load(preset_type, row->get_name());
    }));

    connections.push_back(save_btn->signal_clicked().connect(
        [=]() { app->presets_manager->save(preset_type, name); }));

    connections.push_back(remove_btn->signal_clicked().connect([=]() {
      app->presets_manager->remove(preset_type, name);

      populate_listbox(preset_type);
    }));

    listbox->add(*row);
    listbox->show_all();

    /*if the preset with the name in the button label still exists we do
    not reset the label to "Presets"
    */

    if (name == settings->get_string("last-used-preset")) {
      reset_menu_button_label = false;
    }
  }

  if (reset_menu_button_label) {
    settings->set_string("last-used-preset", _("Presets"));
  }
}
