#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace havel {
class IO;
}

namespace havel::host {

class MouseService {
public:
  enum class Button { Left = 1, Right = 2, Middle = 3, Back = 4, Forward = 5 };
  enum class Action { Click = 0, Press = 1, Release = 2 };

  static void setIO(havel::IO* io) { s_io = io; }

  static Button parseButton(const std::string& button);
  static Button parseButton(int button);
  static Action parseAction(const std::string& action);
  static Action parseAction(int action);

  static void click(Button button = Button::Left);
  static void click(const std::string& button);
  static void click(int button);

  static void press(Button button);
  static void press(const std::string& button);
  static void press(int button);

  static void release(Button button);
  static void release(const std::string& button);
  static void release(int button);

  static void click(Button button, Action action);
  static void click(const std::string& button, const std::string& action);
  static void click(int button, int action);

  static void move(int x, int y, int speed = 5, float accel = 1.0f);
  static void moveRel(int dx, int dy, int speed = 5, float accel = 1.0f);
  static void scroll(int dy, int dx = 0);
  static std::pair<int, int> pos();

  static void setSpeed(int speed);
  static void setAccel(float accel);
  static void setDPI(int dpi);
  static int getSpeed();
  static float getAccel();

private:
  static havel::IO* s_io;
  static int currentSpeed_;
  static float currentAccel_;
};

} // namespace havel::host
