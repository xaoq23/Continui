# 07 — platform：Win32 窗口与事件泵

## 职责边界

platform 隔离操作系统细节：窗口类注册、消息循环、事件翻译、DPI 感知、原生句柄访问。公共 API 不暴露 HWND（`platform_windows.hpp` 提供受控的 `native_window_handle` 给 backend）。

## 公共 API 地图

- `App::create/run/run_one_frame/set_frame_callback/request_redraw/quit/running`（`app.hpp`）
- `Window::set_event_handler/show/hide/visible/client_size/valid`（`window.hpp`）
- `platform_windows::native_window_handle(const Window&) -> void*`（仅受控消费者）

## 内部实现

### App（`src/platform/windows/platform_app.cpp`）

- `AppImpl`：HINSTANCE、窗口集合、frame callback、quit 标志、running 状态
- `register_window_class()` `:18`：注册窗口类（WNDCLASSEX）
- `make_process_dpi_aware()` `:68`：尽力设置 per-monitor DPI 感知（细节属 Phase 2）
- `run()` `:141`：管理循环（注意：0.3 约束下 `run()` 不驱动 frame callback，GPU 应用必须用 `run_one_frame()`）
- `run_one_frame()` `:158`：非阻塞泵消息 → 调用已注册的 frame callback 一次
- `set_frame_callback()` `:179`：单槽（只允许一个消费者，渲染器注册）；渲染器 dtor 注销
- `request_redraw()` `:189`：触发窗口重绘；`quit()` `:200` 终止（WM_QUIT 是线程队列全局的，有 drain 注意事项）

### Window（`src/platform/windows/platform_window.cpp`）

- `window_procedure()` `:99`：WM_CLOSE → `window_close_requested`；WM_SIZE → `window_resized`；键盘/鼠标消息翻译为 `Event`（`is_button_down_message()` `:76`、`deliver()` `:86`）
- `Window::set_event_handler()` `:198`：可替换的 `std::function`；异常直接传播到 `run()/run_one_frame()`（ADR-019）
- `show/hide/visible/client_size/valid()` `:204` 起

## 不变量

1. 所有 App/Window 方法与事件投递在同一线程（主线程）；0.2 阶段不强制检查
2. Window 是 App 拥有的非拥有句柄，不得超出 App 生命期
3. 鼠标 `Event::position` 是客户区像素；坐标直接喂给 `UI::handle_event` → `hit_test`

## 错误路径

`WindowDesc` 宽/高非正 → `invalid_desc`；窗口类/窗口创建失败 → `creation_failed`；App 已 moved-from → `app_invalid`。消息泵不捕获回调异常。

## 已知限制

- Windows IME 已通过 TSF（`ime_tsf.cpp`，架构 §8.3）接入，翻译为 composition_* 事件；`UI::ime_context()` → `Window::set_ime_context()` → TSF 同步文本/caret/文本范围（屏幕坐标），候选窗按 caret 定位；真实中文输入法需手工验收（无法自动化测试）
- 无 touchpad、剪贴板、Monitor/DPI 事件（Phase 2）
- `request_redraw` 是旧接口，真正的失效走 `UI::invalidate`
- 键盘输入已接入 UI（Tab/Enter/Space/方向键/Backspace/Delete + `WM_CHAR` 文本输入）

## 验证入口

`tests/platform/window_test.cpp`：创建/显示/缩放/关闭/事件投递；后端测试的 `SetWindowPos` 真实 resize。

## 溯源

架构 §4.7/§4.8、ADR-019。窗口尺寸轮询由 backend 每帧 `GetClientRect`（0.3 interim）。
