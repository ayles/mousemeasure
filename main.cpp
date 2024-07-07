#include <GL/glew.h>
#include <GLFW/glfw3.h>


#if defined(__linux__)
#include <fcntl.h>
#include <linux/uinput.h>
#include <unistd.h>
#elif _WIN32
#include <Windows.h>
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

#define ENSURE(x) if (!(x)) { std::cerr << __FILE__ << ":" << __LINE__ << std::endl; abort(); }


class ICursor {
    friend class TWindow;
public:
    virtual ~ICursor() = default;

    virtual void Move(double dx, double dy) = 0;

    void SetMoveCallback(std::function<void(double, double)> callback) {
        Callback_ = std::move(callback);
    }

private:
    void OnMove(double x, double y) {
        Callback_(x - X_, y - Y_);
        X_ = x;
        Y_ = y;
    }

private:
    double X_ = 0.0;
    double Y_ = 0.0;

    std::function<void(double, double)> Callback_;
};


#if defined(__linux__)

class TCursor : public ICursor {
public:
    TCursor() {
        uinput_setup uin;
        memset(&uin, 0, sizeof(uin));
        uin.id.bustype = BUS_USB;
        uin.id.vendor = 0x1234;
        uin.id.product = 0x5678;
        strcpy(uin.name, "Test Device");

        Fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        ENSURE(Fd_ != -1);

        // Strangely, libinput will not detect device without this event being enabled
        ENSURE(ioctl(Fd_, UI_SET_EVBIT, EV_KEY) == 0);
        ENSURE(ioctl(Fd_, UI_SET_KEYBIT, BTN_LEFT) == 0);

        ENSURE(ioctl(Fd_, UI_SET_EVBIT, EV_REL) == 0);
        ENSURE(ioctl(Fd_, UI_SET_RELBIT, REL_X) == 0);
        ENSURE(ioctl(Fd_, UI_SET_RELBIT, REL_Y) == 0);
        ENSURE(ioctl(Fd_, UI_DEV_SETUP, &uin) == 0);
        ENSURE(ioctl(Fd_, UI_DEV_CREATE) == 0);
    }

    ~TCursor() {
        ioctl(Fd_, UI_DEV_DESTROY);
        close(Fd_);
    }

    void Move(double dx, double dy) override {
        auto emit = [](int fd, int type, int code, int val) {
            input_event ie;

            ie.type = type;
            ie.code = code;
            ie.value = val;

            /* timestamp values below are ignored */
            ie.time.tv_sec = 0;
            ie.time.tv_usec = 0;

            auto _ = write(fd, &ie, sizeof(ie));
        };

        emit(Fd_, EV_REL, REL_X, dx);
        emit(Fd_, EV_REL, REL_Y, dy);
        emit(Fd_, EV_SYN, SYN_REPORT, 0);
    }

private:
    int Fd_;
};

#elif _WIN32

class TCursor : public ICursor {
public:
    void Move(double dx, double dy) override {
        INPUT event;
        ZeroMemory(&event, sizeof(INPUT));
        event.type = INPUT_MOUSE;
        event.mi.dx = dx;
        event.mi.dy = dy;
        event.mi.dwFlags = MOUSEEVENTF_MOVE;
        ENSURE(SendInput(1, &event, sizeof(INPUT)) == 1);
    }
};

#endif


class IKeyboard {
    friend class TWindow;
public:
    virtual ~IKeyboard() = default;

    void SetKeyCallback(std::function<void(int, int, int, int)> callback) {
        Callback_ = std::move(callback);
    }

private:
    std::function<void(int, int, int, int)> Callback_;
};


class TWindow {
public:
    TWindow(GLFWwindow* window) {
        Cursor_ = std::make_shared<TCursor>();
        Keyboard_ = std::make_shared<IKeyboard>();
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        glfwSetWindowUserPointer(window, this);
        glfwSetCursorPosCallback(window, [](GLFWwindow* window, double x, double y) {
            static_cast<TWindow*>(glfwGetWindowUserPointer(window))->Cursor_->OnMove(x, y);
        });
        glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods){
            static_cast<TWindow*>(glfwGetWindowUserPointer(window))->Keyboard_->Callback_(key, scancode, action, mods);
        });
    }

    ICursor* GetCursor() {
        return Cursor_.get();
    }

    IKeyboard* GetKeyboard() {
        return Keyboard_.get();
    }
private:
    std::shared_ptr<ICursor> Cursor_;
    std::shared_ptr<IKeyboard> Keyboard_;
};


void DrawLine(const std::array<float, 3>& color, int width, int height, std::deque<std::tuple<double, double, uint64_t>> line) {
    glColor3fv(color.data());
    glBegin(GL_LINE_STRIP);
    for (auto& point : line) {
        auto& [x, y, t] = point;
        glVertex2f((x / width) * 2 - 1, -((y / height) * 2 - 1));
    }
    glEnd();
}

inline uint64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

using TLine = std::deque<std::tuple<double, double, uint64_t>>;

int main(int argc, const char** argv) {
    ENSURE(glfwInit() == GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1024, 1024, "Mouse Measure", nullptr, nullptr);
    TWindow w(window);



    TLine line(1000, {512, 512, NowNs()});
    TLine rewindLine;
    std::atomic<bool> rewind;

    w.GetCursor()->SetMoveCallback([&](double dx, double dy) {
        auto& l = rewind ? rewindLine : line;
        l.emplace_back(std::get<0>(l.back()) + dx, std::get<1>(l.back()) + dy, NowNs());
        l.pop_front();
    });

    std::thread t;
    w.GetKeyboard()->SetKeyCallback([&](int key, int, int action, int) {
        if (key == GLFW_KEY_R && action == GLFW_PRESS && !rewind) {
            rewind = true;
            rewindLine = TLine(1000, line[0]);
            if (t.joinable()) {
                t.join();
            }
            t = std::thread([&]() {
                auto cursor = w.GetCursor();
                auto current = NowNs();
                for (size_t i = 1; i < line.size(); ++i) {
                    auto& [x0, y0, t0] = line[i - 1];
                    auto& [x1, y1, t1] = line[i];
                    auto next = current + (t1 - t0);
                    while (NowNs() < next) {}
                    current = next;
                    cursor->Move(x1 - x0, y1 - y0);
                }
                rewind = false;
            });
        }
    });

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    ENSURE(glewInit() == GLEW_OK);

    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);

        DrawLine({ 1, 1, 1, }, width, height, line);
        DrawLine({ 1, 0, 0, }, width, height, rewindLine);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
