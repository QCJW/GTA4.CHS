#pragma once
#include "../common/stdinc.h"
#include "byte_pattern.h"
#include <initializer_list>
#include <string>
#include <vector>

class batch_matching
{
  public:
    typedef std::function<void(const byte_pattern::result_type &)> callback_type;

    struct match_step
    {
        bool run_callback = true;
        bool required = true; // 非必需步骤没命中只跳过，不拖垮整个插件
        std::string group;    // 分组：组内任一必需步骤失败则整组回调全部跳过（如邮件模块）
        std::string name;
        std::vector<std::string> patterns; // 多候选特征码，按顺序第一个命中期望数量的生效
        std::size_t expected_size = 0;
        callback_type callback;
        byte_pattern::result_type result;
        bool succeeded = false;
    };

    // 单特征码登记（保持与原接口兼容）
    void register_step(const char *pattern, std::size_t expected_size, callback_type callback,
                       bool run_callback = true, const char *group = nullptr, bool required = true);

    // 多候选特征码登记：用于同一逻辑站点在不同小版本（如 1.2.0.43 / 1.2.0.59）编码不同
    void register_step_candidates(std::initializer_list<const char *> patterns, std::size_t expected_size,
                                  callback_type callback, const char *name,
                                  bool run_callback = true, const char *group = nullptr,
                                  bool required = true);

    void clear();
    bool perform_search(); // 返回是否进行过搜索
    bool is_all_succeed() const;
    void run_callbacks() const;

  private:
    // 用 vector 保证诊断输出顺序稳定
    std::vector<match_step> _steps;

    bool group_succeeded(const std::string &group) const;
};
