#include "batch_matching.h"
#include <windows.h>

void batch_matching::register_step(const char *pattern, std::size_t expected_size, callback_type callback,
                                   bool run_callback, const char *group, bool required)
{
    match_step step;
    step.run_callback = run_callback;
    step.required = required;
    step.group = group ? group : "";
    step.name = pattern;
    step.patterns.emplace_back(pattern);
    step.expected_size = expected_size;
    step.callback = std::move(callback);

    _steps.emplace_back(std::move(step));
}

void batch_matching::register_step_candidates(std::initializer_list<const char *> patterns,
                                              std::size_t expected_size, callback_type callback,
                                              const char *name, bool run_callback,
                                              const char *group, bool required)
{
    match_step step;
    step.run_callback = run_callback;
    step.required = required;
    step.group = group ? group : "";
    step.name = name ? name : (patterns.size() ? *patterns.begin() : "candidate");
    for (auto p : patterns)
    {
        step.patterns.emplace_back(p);
    }
    step.expected_size = expected_size;
    step.callback = std::move(callback);

    _steps.emplace_back(std::move(step));
}

void batch_matching::clear()
{
    _steps.clear();
}

bool batch_matching::perform_search()
{
    byte_pattern pattern_obj;

    for (auto &step : _steps)
    {
        // 多候选：按顺序取第一个命中数量恰好等于期望值的特征码
        for (const auto &pat : step.patterns)
        {
            pattern_obj.set_pattern(pat.c_str());
            pattern_obj.search();
            step.result = pattern_obj.get();

            if (step.result.size() == step.expected_size)
            {
                step.succeeded = true;
                break;
            }
        }
    }

    return true;
}

bool batch_matching::group_succeeded(const std::string &group) const
{
    if (group.empty())
    {
        return true;
    }

    for (const auto &step : _steps)
    {
        if (step.group == group && step.required && !step.succeeded)
        {
            return false;
        }
    }

    return true;
}

bool batch_matching::is_all_succeed() const
{
    for (const auto &step : _steps)
    {
        if (!step.succeeded)
        {
            if (!step.required)
            {
                continue; // 可选步骤：跳过即可
            }

            if (!step.group.empty())
            {
                continue; // 分组步骤：整组降级，不拖垮其余汉化
            }

            return false; // 核心必需步骤失败：拒绝加载，避免写飞
        }
    }

    return true;
}

void batch_matching::run_callbacks() const
{
    for (const auto &step : _steps)
    {
        if (step.run_callback && step.succeeded && group_succeeded(step.group))
        {
            step.callback(step.result);
        }
    }
}
