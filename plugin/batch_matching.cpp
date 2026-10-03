#include "batch_matching.h"

void batch_matching::register_step(const char *pattern, std::size_t expected_size, callback_type callback,
                                   bool run_callback)
{
    match_step step;
    step.run_callback = run_callback;
    step.expected_size = expected_size;
    step.callback = std::move(callback);

    _steps.emplace(pattern, std::move(step));
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
        // 写入缓存的数据，验证不成功时才进行搜索
        pattern_obj.set_pattern(step.first.c_str());

        pattern_obj.search();
        step.second.result = pattern_obj.get();
    }

    return true;
}

bool batch_matching::is_all_succeed() const
{
    return std::ranges::all_of(_steps, [](const std::pair<std::string, match_step> &step) {
        return step.second.expected_size == step.second.result.size();
    });
}

void batch_matching::run_callbacks() const
{
    for (auto &step : _steps)
    {
        if (step.second.run_callback)
        {
            step.second.callback(step.second.result);
        }
    }
}
