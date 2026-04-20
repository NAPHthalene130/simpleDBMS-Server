# 项目开发规范
## 1. 命名规范
### 1.1 核心命名规则
| 元素类型       | 命名规则                          | 示例                     |
|----------------|-----------------------------------|--------------------------|
| 类名/结构体名  | **大驼峰命名法**（PascalCase）   | `UserManager`, `DataModel` |
| 方法名/函数名  | **小驼峰命名法**（camelCase）    | `getUserName()`, `calculateTotal()` |
| 变量名（局部/成员） | **小驼峰命名法**（camelCase） | `userName`, `totalCount` |
| 包名/命名空间  | **小驼峰命名法**（camelCase）    | `userService`, `utils`   |
| 常量/宏定义    | **全大写下划线分割**（UPPER_SNAKE_CASE） | `MAX_BUFFER_SIZE`, `PI` |

### 1.2 文件命名
- 头文件：`.h`，使用小驼峰命名，如 `userManager.h`
- 源文件：`.cpp`，与对应头文件同名，如 `userManager.cpp`


## 2. 代码格式规范
### 2.1 缩进与换行
- 使用 **4 个空格**缩进（禁止使用 Tab）
- 函数/类之间空 1-2 行，逻辑块之间空 1 行
- 单行代码不超过 **120 字符**，超出时合理换行

### 2.2 大括号风格
采用 **K&R 风格**（左大括号不换行）：
```cpp
class UserManager {
public:
    void addUser(const std::string& name) {
        if (name.empty()) {
            return;
        }
        users.push_back(name);
    }
private:
    std::vector<std::string> users;
};
```


## 3. 注释规范
### 3.1 方法/函数注释
使用 **Doxygen 风格**注释，包含以下标签：
```cpp
/**
 * @brief 添加新用户
 * @author ZhangSan
 * @param userName 用户名（不可为空）
 * @param age 用户年龄（范围：0-150）
 * @return 是否添加成功
 * @note 若用户名已存在，返回 false
 */
bool addUser(const std::string& userName, int age);
```

### 3.2 类注释
在类定义前添加职责说明：
```cpp
/**
 * @class UserManager
 * @brief 负责用户信息的增删改查管理
 * @details 内部使用 vector 存储用户数据，支持线程安全操作
 */
class UserManager { ... };
```

### 3.3 行内注释
- 仅对复杂逻辑、非直观代码添加注释
- 注释位于代码上方或右侧，使用 `//` 开头

### 3.4 注释语言
- 所有注释均采用 **中文**，非必要避免使用英文

## 4. 项目构建与结构
### 4.1 CMakeLists.txt 规范
- **非必要不修改** CMakeLists.txt，如需修改需在提交说明中阐述原因
- 保持模块化结构，按功能划分目录
- 第三方依赖通过 `find_package()` 或 `FetchContent` 管理，避免硬编码路径


## 5. 面向对象设计规范
### 5.1 封装与访问控制
- 类属性默认设为 `private` 或 `protected`，**禁止直接暴露 public 成员变量**
- 通过 `get/set` 方法访问和修改属性，方法声明在 `.h` 头文件中，定义在 `.cpp` 源文件中：

**userManager.h（头文件）**
```cpp
class User {
private:
    std::string name;
    int age;
public:
    // Getter 声明
    std::string getName() const;
    int getAge() const;
    // Setter 声明
    void setAge(int newAge);
};
```

**userManager.cpp（源文件）**
```cpp
#include "userManager.h"

// Getter 定义
std::string User::getName() const {
    return name;
}

int User::getAge() const {
    return age;
}

// Setter 定义（可添加参数校验）
void User::setAge(int newAge) {
    if (newAge > 0 && newAge < 150) {
        age = newAge;
    }
}
```

### 5.2 解耦与模块化
- 遵循**单一职责原则**：一个类/函数只负责一个功能
- 优先使用**接口/抽象类**实现依赖倒置，减少具体类之间的耦合
- 避免循环依赖，可通过中间层或依赖注入解决

### 5.4 方法参数传入
- 对于 **类对象或较大结构体**，方法参数应优先采用 **指针传递**（`const UserInfo* userInfo`），避免复制大对象并便于表达可空语义
- 对于 **字符串、基本类型、枚举等轻量类型**，优先直接传值（`std::string userName`、`int age`、`bool enabled`）
- 对于可选参数，使用 **默认值**（`int age = 0`），避免调用者忘记传递参数

## 6. 头文件规范
### 6.1 头文件保护
使用 `#pragma once`（跨平台兼容性好）或传统的 `#ifndef` 保护：
```cpp
#pragma once  // 推荐

// 或传统方式
#ifndef USER_MANAGER_H
#define USER_MANAGER_H
// 代码内容
#endif
```

### 6.2 引用顺序
按以下顺序包含头文件，每组之间空一行：
1. 标准库头文件（`<iostream>`, `<vector>` 等）
2. 第三方库头文件（`<boost/...>`, `<gtest/...>` 等）
3. 项目内头文件（`"userManager.h"` 等）


## 7. 内存管理
- 优先使用 **智能指针**（`std::unique_ptr`, `std::shared_ptr`），避免裸指针 `new/delete`
- 如需手动管理内存，确保 `new/delete`、`new[]/delete[]` 配对使用
- 容器类（`std::vector`, `std::string`）优先替代手动分配的数组


## 8. 异常处理
- 仅在**不可恢复的错误**场景使用异常，正常流程用返回值（如 `bool`、`std::optional`）
- 自定义异常继承自 `std::exception`：
  ```cpp
  class UserNotFoundException : public std::exception {
  public:
      const char* what() const noexcept override {
          return "User not found";
      }
  };
  ```
## 8. 控制台输出
- 所有输出均采用 **英文**
- 避免使用中文输出
- 控制台输出日志应该满足以下格式:
    - 时间戳（YYYY-MM-DD HH:MM:SS）
    - 日志级别（DEBUG, INFO, WARNING, ERROR, FATAL）
    - 日志消息（详细描述事件或错误信息）
    - 例如：`[2023-10-01 12:00:00][DEBUG] UserManager::addUser("ZhangSan")`
