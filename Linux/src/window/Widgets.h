#pragma once

// Small building blocks shared by the window's pages.
#include <string>

#include <adwaita.h>

namespace sukut::window {

// Tooltip and accessible label for a control whose meaning is only an icon.
// The name is also kept for the keyboard check (--capture).
void Name(GtkWidget* widget, const std::string& name);
const char* NameOf(GtkWidget* widget);

GtkWidget* Label(const std::string& text, const char* css_class = nullptr, bool wrap = true);
GtkWidget* Icon(const char* icon_name, const char* css_class = nullptr);
GtkWidget* IconButton(const char* icon_name, const std::string& name);
void AddClasses(GtkWidget* widget, std::initializer_list<const char*> classes);
void SetClass(GtkWidget* widget, const char* css_class, bool on);

// The sleeping face from the window's power button on Windows: an accent
// circle with closed eyes and "z z". Drawn left-to-right in every language.
GtkWidget* SleepFace(int size);

// The rainbow الطرفية credit (Handjet). It animates only while some of it is
// inside its scrolled page's viewport and animations are on; high contrast
// keeps it still.
GtkWidget* BrandCredit();

// A help card: a coloured icon and title, then bullet points.
GtkWidget* HelpCard(const char* icon_name, const char* tone, const std::string& title, std::initializer_list<std::string> points);

}  // namespace sukut::window
