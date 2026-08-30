#ifndef SCROLLER_ENUMS_H
#define SCROLLER_ENUMS_H

#include <string>
#include <optional>

enum class Direction { Left, Right, Up, Down, Begin, End, Center, Middle, Invalid };
enum class AdmitExpelDirection { Left, Right };
enum class FitSize { Active, Visible, All, ToEnd, ToBeg };
enum class Mode { Row, Column, Toggle };

class ModeModifier {
public:
    enum {
        POSITION_UNDEFINED,
        POSITION_AFTER,
        POSITION_BEFORE,
        POSITION_END,
        POSITION_BEGINNING
    };

    enum {
        FOCUS_UNDEFINED,
        FOCUS_FOCUS,
        FOCUS_NOFOCUS
    };

    enum {
        AUTO_UNDEFINED,
        AUTO_MANUAL,
        AUTO_AUTO
    };
    ModeModifier();
    ModeModifier(const std::string &arg);
    void set_position(int p);
    int get_position(bool force_default = true) const;
    std::string get_position_string() const;
    void set_focus(int f);
    int get_focus(bool force_default = true) const;
    std::string get_focus_string() const;
    void set_auto_mode(int mode);
    void set_auto_param(int param);
    int get_auto_mode(bool force_default = true) const;
    std::string get_auto_mode_string() const;
    int get_auto_param() const;

    void set_center_column(bool c);
    bool center_column_enabled() const;
    std::optional<bool> center_column_override() const;
    std::string get_center_column_string() const;
    void set_center_window(bool c);
    bool center_window_enabled() const;
    std::optional<bool> center_window_override() const;
    std::string get_center_window_string() const;

private:
    int position = POSITION_UNDEFINED;
    int focus = FOCUS_UNDEFINED;
    int auto_mode = AUTO_UNDEFINED;
    int auto_param = 2;
    std::optional<bool> center_column;
    std::optional<bool> center_window;
};

#endif  // SCROLLER_ENUMS_H
