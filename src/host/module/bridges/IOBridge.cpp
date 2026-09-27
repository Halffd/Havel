// IOBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void IOBridge::install(PipelineOptions &options) {
    options.host_functions["send"] = [ctx = ctx_](const auto &args) {
        return handleSend(args, ctx);
    };
    options.host_functions["io.send"] = [ctx = ctx_](const auto &args) {
        return handleSend(args, ctx);
    };
    options.host_functions["io.sendKey"] = [ctx = ctx_](const auto &args) {
        return handleSendKey(args, ctx);
    };
    options.host_functions["io.sendText"] = [ctx = ctx_](const auto &args) {
        return handleSendText(args, ctx);
    };
    options.host_functions["io.wait"] = [ctx = ctx_](const auto &args) {
        return handleWait(args, ctx);
    };
    options.host_functions["wait"] = [ctx = ctx_](const auto &args) {
        return handleWait(args, ctx);
    };
    // Mouse functions
    options.host_functions["io.click"] = [ctx = ctx_](const auto &args) {
        return handleMouseClick(args, ctx);
    };
    options.host_functions["io.mouseMoveTo"] = [ctx = ctx_](const auto &args) {
        return handleMouseMoveTo(args, ctx);
    };
    options.host_functions["io.mouseMoveRel"] = [ctx = ctx_](const auto &args) {
        return handleMouseMoveRel(args, ctx);
    };
    options.host_functions["io.mouseScroll"] = [ctx = ctx_](const auto &args) {
        return handleMouseScroll(args, ctx);
    };
    options.host_functions["io.scroll"] = [ctx = ctx_](const auto &args) {
        return handleMouseScroll(args, ctx);
    };
    options.host_functions["io.mouseDown"] = [ctx = ctx_](const auto &args) {
        return handleMouseDown(args, ctx);
    };
 options.host_functions["io.mouseUp"] = [ctx = ctx_](const auto &args) {
 return handleMouseUp(args, ctx);
 };
 options.host_functions["mouse.click"] = [ctx = ctx_](const auto &args) {
 return handleMouseClick(args, ctx);
 };
 options.host_functions["mouse.down"] = [ctx = ctx_](const auto &args) {
 return handleMouseDown(args, ctx);
 };
 options.host_functions["mouse.up"] = [ctx = ctx_](const auto &args) {
 return handleMouseUp(args, ctx);
 };
 options.host_functions["mouse.move"] = [ctx = ctx_](const auto &args) {
 return handleMouseMoveTo(args, ctx);
 };
 options.host_functions["mouse.moveRel"] = [ctx = ctx_](const auto &args) {
 return handleMouseMoveRel(args, ctx);
 };
 options.host_functions["mouse.scroll"] = [ctx = ctx_](const auto &args) {
 return handleMouseScroll(args, ctx);
 };
 options.host_functions["mouse.pos"] = [ctx = ctx_](const auto &args) {
 return handleMousePos(args, ctx);
 };
 options.host_functions["mouse.setSpeed"] = [ctx = ctx_](const auto &args) {
 return handleMouseSetSpeed(args, ctx);
 };
 options.host_functions["mouse.setAccel"] = [ctx = ctx_](const auto &args) {
 return handleMouseSetAccel(args, ctx);
 };
 options.host_functions["mouse.setDPI"] = [ctx = ctx_](const auto &args) {
 return handleMouseSetDPI(args, ctx);
 };
 options.host_functions["keyDown"] = [ctx = ctx_](const auto &args) {
 return handleKeyDown(args, ctx);
 };
 options.host_functions["keyUp"] = [ctx = ctx_](const auto &args) {
 return handleKeyUp(args, ctx);
 };
options.host_functions["suspend"] = [ctx = ctx_](const auto &args) {
    return handleSuspend(args, ctx);
};
 options.host_functions["io.getExecutorMode"] = [ctx = ctx_](const auto &args) {
     return handleGetExecutorMode(args, ctx);
 };
 options.host_functions["io.setExecutorMode"] = [ctx = ctx_](const auto &args) {
     return handleSetExecutorMode(args, ctx);
 };
  options.host_functions["io.getKey"] = [ctx = ctx_](const auto &args) {
      return handleGetKey(args, ctx);
  };
  options.host_functions["io.isKeyPressed"] = [ctx = ctx_](const auto &args) {
      return handleIsKeyPressed(args, ctx);
  };
    options.host_functions["io.state"] = [ctx = ctx_](const auto &args) {
        if (args.empty()) {
            if (!ctx->io) return Value::makeBool(false);
            return Value::makeBool(ctx->io->IsAnyKeyPressed());
        }
        return handleGetKey(args, ctx);
    };
  options.host_functions["io.sendModifiers"] = [ctx = ctx_](const auto &args) {
      return handleSendModifiers(args, ctx);
  };
  options.host_functions["io.sendKey"] = [ctx = ctx_](const auto &args) {
      return handleSendKeyState(args, ctx);
  };
}


Value IOBridge::handleSend(const std::vector<Value> &args,
                          const HostContext *ctx) {
    if (args.empty() || !ctx->io) {
        return Value::makeBool(false);
    }

    std::string keys;
    if (args[0].isStringValId() || args[0].isStringId()) {
        auto *vm = static_cast<VM *>(ctx->vm);
        keys = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
    } else {
        return Value::makeBool(false);
    }

	ctx->io->Send(keys.c_str());
	return Value::makeBool(true);
}


Value IOBridge::handleSendKey(const std::vector<Value> &args,
                              const HostContext *ctx) {
    if (args.empty() || !ctx->io) {
        return Value::makeBool(false);
    }

    std::string key;
    if (args[0].isStringValId() || args[0].isStringId()) {
        auto *vm = static_cast<VM *>(ctx->vm);
        key = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
    } else {
        return Value::makeBool(false);
    }

	ctx->io->SendX11Key(key, true);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	ctx->io->SendX11Key(key, false);
	return Value::makeBool(true);
}


Value IOBridge::handleSendText(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return Value::makeBool(false);
  }
  // TODO: string support - disabled until string pooling is implemented
  (void)args;
  return Value::makeBool(false);
#if 0
  if (false) { // TODO: string support
    // Use clipboard for reliable text input (handles all characters, spaces,
    // newlines)
    if (ctx->clipboardManager) {
      // Backup old clipboard text
      QString oldText = ctx->clipboardManager->getClipboard()->text();

      // Set new text
      ctx->clipboardManager->getClipboard()->setText(
          QString::fromStdString(*text));

      // Minimal delay - just enough for clipboard to sync
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Send Ctrl+V to paste
      ctx->io->Send("{LCtrl down}");
      ctx->io->Send("v");
      ctx->io->Send("{LCtrl up}");

      // Restore old clipboard
      ctx->clipboardManager->getClipboard()->setText(oldText);
    } else {
      // Fallback: use IO::SendText (handles clipboard backup/restore on
      // Windows) or key events on Linux
      ctx->io->SendText(*text);
    }
    return Value::makeBool(true);
  }
  return Value::makeBool(false);
#endif
}


Value IOBridge::handleWait(const std::vector<Value> &args,
                         const HostContext *ctx) {
    if (args.empty() || !args[0].isInt()) {
        return Value::makeBool(false);
    }
    int64_t ms = args[0].asInt();
    // io.wait: same semantics as timer.after — sleep on a worker so
    // goroutines park; top-level blocks inline exactly as before.
    if (!ctx || !ctx->vm) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return Value::makeBool(true);
    }
    auto *vm = static_cast<VM *>(ctx->vm);
    compiler::VMApi api(*vm);
    return api.runBlocking(
        [ms]() -> compiler::AsyncCxxResult {
          std::this_thread::sleep_for(std::chrono::milliseconds(ms));
          return nullptr;
        },
        [](const compiler::AsyncCxxResult &) -> Value {
          return Value::makeBool(true);
        });
}


Value IOBridge::handleMouseClick(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  auto *io = ctx ? ctx->io : nullptr;
  if (!io) return Value::makeBool(false);

  auto button = ::havel::host::MouseService::Button::Left;
  if (!args.empty()) {
    if (args[0].isStringValId() || args[0].isStringId()) {
      auto *vm = static_cast<VM *>(ctx->vm);
      std::string btnStr = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
      button = ::havel::host::MouseService::parseButton(btnStr);
    } else if (args[0].isInt()) {
      button = ::havel::host::MouseService::parseButton(static_cast<int>(args[0].asInt()));
    }
  }

  int btnInt = static_cast<int>(button);

  if (args.size() >= 2 && (args[1].isStringValId() || args[1].isStringId())) {
    auto *vm = static_cast<VM *>(ctx->vm);
    std::string action = vm ? vm->resolveStringKey(args[1]) : strVal(args[1], ctx ? ctx->vm : nullptr);
    if (action == "down") {
      io->MouseDown(btnInt);
      return Value::makeBool(true);
    } else if (action == "up") {
      io->MouseUp(btnInt);
      return Value::makeBool(true);
    }
  }

  io->MouseClick(btnInt);
  return Value::makeBool(true);
}


Value IOBridge::handleMouseMoveTo(const std::vector<Value> &args,
                                 const HostContext *ctx) {
    auto *io = ctx ? ctx->io : nullptr;
    if (!io || args.size() < 2) {
        return Value::makeBool(false);
    }

    int x = args[0].isInt() ? static_cast<int>(args[0].asInt()) :
             args[0].isDouble() ? static_cast<int>(args[0].asDouble()) : 0;
    int y = args[1].isInt() ? static_cast<int>(args[1].asInt()) :
             args[1].isDouble() ? static_cast<int>(args[1].asDouble()) : 0;
    int speed = args.size() > 2 && args[2].isInt() ? static_cast<int>(args[2].asInt()) : 5;
    float accel = args.size() > 3 && args[3].isDouble() ? static_cast<float>(args[3].asDouble()) : 1.0f;

    io->MouseMoveTo(x, y, speed, accel);
    return Value::makeBool(true);
}


Value IOBridge::handleMouseMoveRel(const std::vector<Value> &args,
                                  const HostContext *ctx) {
    auto *io = ctx ? ctx->io : nullptr;
    if (!io || args.size() < 2) {
        return Value::makeBool(false);
    }

    int dx = args[0].isInt() ? static_cast<int>(args[0].asInt()) :
              args[0].isDouble() ? static_cast<int>(args[0].asDouble()) : 0;
    int dy = args[1].isInt() ? static_cast<int>(args[1].asInt()) :
              args[1].isDouble() ? static_cast<int>(args[1].asDouble()) : 0;
    int speed = args.size() > 2 && args[2].isInt() ? static_cast<int>(args[2].asInt()) : 5;
    float accel = args.size() > 3 && args[3].isDouble() ? static_cast<float>(args[3].asDouble()) : 1.0f;

    io->MouseMove(dx, dy, speed, accel);
    return Value::makeBool(true);
}


Value IOBridge::handleMouseScroll(const std::vector<Value> &args,
                                 const HostContext *ctx) {
    auto *io = ctx ? ctx->io : nullptr;
    if (!io || args.empty()) {
        return Value::makeBool(false);
    }

    int dy = args[0].isInt() ? static_cast<int>(args[0].asInt()) :
                 args[0].isDouble() ? static_cast<int>(args[0].asDouble()) : 0;
    int dx = args.size() > 1 && args[1].isInt() ? static_cast<int>(args[1].asInt()) : 0;

    if (debugging::debug_io) debug("[HOST] mouse.scroll dy={} dx={}", dy, dx);
    io->Scroll(dy, dx);
    return Value::makeBool(true);
}


Value IOBridge::handleMouseDown(const std::vector<Value> &args,
                               const HostContext *ctx) {
    auto *io = ctx ? ctx->io : nullptr;
    if (!io) return Value::makeBool(false);

    auto button = ::havel::host::MouseService::Button::Left;
    if (!args.empty()) {
        if (args[0].isStringValId() || args[0].isStringId()) {
            auto *vm = static_cast<VM *>(ctx->vm);
            std::string btnStr = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
            button = ::havel::host::MouseService::parseButton(btnStr);
        } else if (args[0].isInt()) {
            button = ::havel::host::MouseService::parseButton(static_cast<int>(args[0].asInt()));
        }
    }

    io->MouseDown(static_cast<int>(button));
    return Value::makeBool(true);
}


Value IOBridge::handleMouseUp(const std::vector<Value> &args,
                             const HostContext *ctx) {
    auto *io = ctx ? ctx->io : nullptr;
    if (!io) return Value::makeBool(false);

    auto button = ::havel::host::MouseService::Button::Left;
    if (!args.empty()) {
        if (args[0].isStringValId() || args[0].isStringId()) {
            auto *vm = static_cast<VM *>(ctx->vm);
            std::string btnStr = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
            button = ::havel::host::MouseService::parseButton(btnStr);
        } else if (args[0].isInt()) {
            button = ::havel::host::MouseService::parseButton(static_cast<int>(args[0].asInt()));
        }
    }

    io->MouseUp(static_cast<int>(button));
    return Value::makeBool(true);
}


Value IOBridge::handleMousePos(const std::vector<Value> &,
 const HostContext *ctx) {
 auto *io = ctx ? ctx->io : nullptr;
 auto [x, y] = io ? io->GetMousePosition() : std::make_pair(0, 0);
 auto *vm = static_cast<VM *>(ctx->vm);
 if (!vm) return Value::makeBool(false);
 auto obj = vm->createHostObject();
 vm->setHostObjectField(obj, "x", Value::makeInt(x));
 vm->setHostObjectField(obj, "y", Value::makeInt(y));
 return Value::makeObjectId(obj.id);
}


Value IOBridge::handleMouseSetSpeed(const std::vector<Value> &args,
 const HostContext *) {
 if (args.empty()) return Value::makeBool(false);
 int speed = args[0].isInt() ? static_cast<int>(args[0].asInt()) :
 args[0].isDouble() ? static_cast<int>(args[0].asDouble()) : 5;
 ::havel::host::MouseService::setSpeed(speed);
 return Value::makeBool(true);
 }


Value IOBridge::handleMouseSetAccel(const std::vector<Value> &args,
 const HostContext *) {
 if (args.empty()) return Value::makeBool(false);
 float accel = args[0].isDouble() ? static_cast<float>(args[0].asDouble()) :
 args[0].isInt() ? static_cast<float>(args[0].asInt()) : 1.0f;
 ::havel::host::MouseService::setAccel(accel);
 return Value::makeBool(true);
 }


Value IOBridge::handleMouseSetDPI(const std::vector<Value> &args,
 const HostContext *) {
 if (args.empty()) return Value::makeBool(false);
 int dpi = args[0].isInt() ? static_cast<int>(args[0].asInt()) :
 args[0].isDouble() ? static_cast<int>(args[0].asDouble()) : 800;
 ::havel::host::MouseService::setDPI(dpi);
 return Value::makeBool(true);
 }


Value IOBridge::handleKeyDown(const std::vector<Value> &args,
 const HostContext *ctx) {
 if (args.empty() || !ctx->io) return Value::makeBool(false);
 std::string key;
 if (args[0].isStringValId() || args[0].isStringId()) {
 auto *vm = static_cast<VM *>(ctx->vm);
 key = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
 } else {
 return Value::makeBool(false);
 }
	ctx->io->SendX11Key(key, true);
	return Value::makeBool(true);
 }


Value IOBridge::handleKeyUp(const std::vector<Value> &args,
 const HostContext *ctx) {
 if (args.empty() || !ctx->io) return Value::makeBool(false);
 std::string key;
 if (args[0].isStringValId() || args[0].isStringId()) {
 auto *vm = static_cast<VM *>(ctx->vm);
 key = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
 } else {
 return Value::makeBool(false);
 }
	ctx->io->SendX11Key(key, false);
	return Value::makeBool(true);
 }


Value IOBridge::handleGetKey(const std::vector<Value> &args,
                              const HostContext *ctx) {
    if (args.empty() || !ctx->io) return Value::makeBool(false);
    std::string key;
    if (args[0].isStringValId() || args[0].isStringId()) {
        auto *vm = static_cast<VM *>(ctx->vm);
        key = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
    } else {
        return Value::makeBool(false);
    }
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "lbutton" || lower == "mouseleft" || lower == "mouse1" ||
        lower == "rbutton" || lower == "mouseright" || lower == "mouse2" ||
        lower == "mbutton" || lower == "mousemiddle" || lower == "mouse3" ||
        lower == "xbutton1" || lower == "mouse4" ||
        lower == "xbutton2" || lower == "mouse5") {
        int button = 1;
        if (lower == "rbutton" || lower == "mouseright" || lower == "mouse2") button = 3;
        else if (lower == "mbutton" || lower == "mousemiddle" || lower == "mouse3") button = 2;
        else if (lower == "xbutton1" || lower == "mouse4") button = 4;
        else if (lower == "xbutton2" || lower == "mouse5") button = 5;
        auto display = havel::DisplayManager::GetDisplay();
        if (!display) return Value::makeBool(false);
        Window root, child;
        int rootX, rootY, winX, winY;
        unsigned int mask;
        if (XQueryPointer(display, DefaultRootWindow(display), &root, &child,
                          &rootX, &rootY, &winX, &winY, &mask)) {
            switch (button) {
            case 1: return Value::makeBool((mask & Button1Mask) != 0);
            case 2: return Value::makeBool((mask & Button2Mask) != 0);
            case 3: return Value::makeBool((mask & Button3Mask) != 0);
            case 4: return Value::makeBool((mask & Button4Mask) != 0);
            case 5: return Value::makeBool((mask & Button5Mask) != 0);
            }
        }
        return Value::makeBool(false);
    }
	return Value::makeBool(ctx->io->GetKeyState(key));
}


Value IOBridge::handleIsKeyPressed(const std::vector<Value> &args,
  const HostContext *ctx) {
 if (args.empty() || !ctx->io) return Value::makeBool(false);
 std::string key;
 if (args[0].isStringValId() || args[0].isStringId()) {
 auto *vm = static_cast<VM *>(ctx->vm);
 key = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
 } else {
 return Value::makeBool(false);
 }
	return Value::makeBool(ctx->io->IsKeyPressed(key));
 }


Value IOBridge::handleSuspend(const std::vector<Value> &,
                              const HostContext *ctx) {
    if (!ctx->io) return Value::makeBool(false);
	if (ctx->io->IsSuspended()) {
		ctx->io->Resume();
		return Value::makeBool(false);
	}
	ctx->io->Suspend();
	return Value::makeBool(true);
}


Value IOBridge::handleGetExecutorMode(const std::vector<Value> &,
                                      const HostContext *ctx) {
    if (!ctx->io) {
        return Value::makeNull();
    }
	auto *vm = static_cast<VM *>(ctx->vm);
	std::string mode;
	switch (ctx->io->GetExecutorMode()) {
	case ExecutorMode::Executor: mode = "executor"; break;
	case ExecutorMode::Sync: mode = "sync"; break;
	case ExecutorMode::Thread: mode = "thread"; break;
	case ExecutorMode::Scheduler: mode = "scheduler"; break;
	default: mode = "scheduler"; break;
	}
	if (!vm) return Value::makeNull();
	auto ref = vm->getHeap().allocateString(mode);
	return Value::makeStringId(ref.id);
}


Value IOBridge::handleSetExecutorMode(const std::vector<Value> &args,
                                      const HostContext *ctx) {
    if (args.empty() || !ctx->io) {
        return Value::makeBool(false);
    }
    auto *vm = static_cast<VM *>(ctx->vm);
    std::string modeStr;
    if (args[0].isStringValId() || args[0].isStringId()) {
        modeStr = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
    } else {
        return Value::makeBool(false);
    }
	std::string modeLower = modeStr;
	std::transform(modeLower.begin(), modeLower.end(), modeLower.begin(), ::tolower);
	ExecutorMode em;
	if (modeLower == "executor") em = ExecutorMode::Executor;
	else if (modeLower == "sync") em = ExecutorMode::Sync;
	else if (modeLower == "thread") em = ExecutorMode::Thread;
	else if (modeLower == "scheduler") em = ExecutorMode::Scheduler;
	else return Value::makeBool(false);
	ctx->io->SetExecutorMode(em);
	return Value::makeBool(true);
}



Value IOBridge::handleSendModifiers(const std::vector<Value> &args,
                                     const HostContext *ctx) {
    if (!ctx->io) return Value::makeBool(false);
    std::string mods;
    bool press = true;
    if (!args.empty()) {
        auto *vm = static_cast<VM *>(ctx->vm);
        if (args[0].isStringId() || args[0].isStringValId())
            mods = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
        if (args.size() > 1) {
            if (args[1].isBool()) press = args[1].asBool();
            else if (args[1].isInt()) press = args[1].asInt() != 0;
            else if (args[1].isStringId() || args[1].isStringValId()) {
                std::string s = vm ? vm->resolveStringKey(args[1]) : strVal(args[1], ctx ? ctx->vm : nullptr);
                press = (s != "release" && s != "up");
            }
        }
    }
	if (mods.empty()) {
		if (!press) {
			ctx->io->SendX11Key("ctrl", false); ctx->io->SendX11Key("shift", false);
			ctx->io->SendX11Key("alt", false); ctx->io->SendX11Key("super", false);
		}
		return Value::makeBool(true);
	}
	std::string lower = mods;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	std::vector<std::string> parts;
	size_t start = 0, pos;
	while ((pos = lower.find('+', start)) != std::string::npos) {
		parts.push_back(lower.substr(start, pos - start));
		start = pos + 1;
	}
	if (start < lower.size()) parts.push_back(lower.substr(start));
	for (auto &p : parts) {
		if (p == "ctrl" || p == "control") {
			ctx->io->SendX11Key("ctrl", press);
		} else if (p == "shift") {
			ctx->io->SendX11Key("shift", press);
		} else if (p == "alt") {
			ctx->io->SendX11Key("alt", press);
		} else if (p == "super" || p == "win" || p == "meta") {
			ctx->io->SendX11Key("super", press);
		}
	}
	return Value::makeBool(true);
}




Value IOBridge::handleSendKeyState(const std::vector<Value> &args,
                                    const HostContext *ctx) {
    if (args.empty() || !ctx->io) return Value::makeBool(false);
    auto *vm = static_cast<VM *>(ctx->vm);
    std::string key;
    if (args[0].isStringId() || args[0].isStringValId())
        key = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
    else return Value::makeBool(false);
	if (args.size() < 2) {
		ctx->io->SendX11Key(key, true);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		ctx->io->SendX11Key(key, false);
		return Value::makeBool(true);
	}
	bool press = true;
	if (args[1].isBool()) press = args[1].asBool();
	else if (args[1].isInt()) press = args[1].asInt() != 0;
	else if (args[1].isStringId() || args[1].isStringValId()) {
		std::string s = vm ? vm->resolveStringKey(args[1]) : strVal(args[1], ctx ? ctx->vm : nullptr);
		if (s == "release" || s == "up") press = false;
		else if (s == "click" || s == "tap") {
			ctx->io->SendX11Key(key, true);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			ctx->io->SendX11Key(key, false);
			return Value::makeBool(true);
		}
	}
	if (press) { ctx->io->SendX11Key(key, true); return Value::makeBool(true); }
	ctx->io->SendX11Key(key, false);
	return Value::makeBool(true);
}


// ============================================================================
// SystemBridge Implementation
// ============================================================================

} // namespace havel::compiler
