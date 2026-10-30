#pragma once

// Persistent snapshots of the conversation-independent prompt prefix (system prompt, tool
// definitions, template preamble), so a new chat restores it instead of prefilling it again.
// Hybrid and recurrent models cannot trim their state back to a shared prefix, so for them
// this is the only prompt reuse there is.

#include "llama.h"
#include "chat.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <utime.h>
#include <vector>

namespace agora::prefix_cache {

// Below this a snapshot costs more file I/O than the prefill it saves.
constexpr size_t MIN_PREFIX_TOKENS = 64;
constexpr size_t MAX_SNAPSHOT_FILES = 4;
constexpr uint64_t MAX_SNAPSHOT_BYTES = 2ull * 1024 * 1024 * 1024;
constexpr const char * SNAPSHOT_SUFFIX = ".kv";

inline size_t common_prefix_length(const std::string & a, const std::string & b) {
    const size_t limit = std::min(a.size(), b.size());
    size_t n = 0;
    while (n < limit && a[n] == b[n]) n++;
    return n;
}

// Renders the leading system messages with two different probe user messages. Whatever both
// renders share comes before any user content, so it is the same for every conversation that
// uses these system messages and tools. Returns the part of `prompt` that matches it.
inline std::string static_prefix(
    const common_chat_templates * templates,
    const common_chat_templates_inputs & inputs,
    const std::string & prompt
) {
    try {
        common_chat_templates_inputs probe = inputs;
        probe.messages.clear();
        for (const auto & message : inputs.messages) {
            if (message.role != "system") break;
            probe.messages.push_back(message);
        }
        probe.add_generation_prompt = true;
        std::string renders[2];
        const char * contents[2] = {"A", "Z"};
        for (int i = 0; i < 2; ++i) {
            common_chat_templates_inputs variant = probe;
            common_chat_msg user;
            user.role = "user";
            user.content = contents[i];
            variant.messages.push_back(std::move(user));
            renders[i] = common_chat_templates_apply(templates, variant).prompt.str();
        }
        size_t length = common_prefix_length(renders[0], renders[1]);
        length = std::min(length, common_prefix_length(renders[0], prompt));
        return prompt.substr(0, length);
    } catch (const std::exception &) {
        return {};
    }
}

// Number of leading prompt tokens covered by `prefix_text`; 0 when too short to be worth it.
inline size_t prefix_token_count(
    const llama_vocab * vocab,
    const std::string & prefix_text,
    const std::vector<llama_token> & prompt_tokens
) {
    if (prefix_text.empty()) return 0;
    std::vector<llama_token> prefix(prefix_text.size() + 8);
    int32_t n = llama_tokenize(vocab, prefix_text.c_str(), prefix_text.size(),
                               prefix.data(), static_cast<int32_t>(prefix.size()), true, true);
    if (n <= 0) return 0;
    prefix.resize(static_cast<size_t>(n));
    size_t shared = 0;
    const size_t limit = std::min(prefix.size(), prompt_tokens.size());
    while (shared < limit && prefix[shared] == prompt_tokens[shared]) shared++;
    // The last prefix token may merge differently once the conversation follows it.
    if (shared == prefix.size() && shared > 0) shared--;
    // Sampling needs at least one prompt token decoded in this request.
    if (shared >= prompt_tokens.size()) return 0;
    return shared >= MIN_PREFIX_TOKENS ? shared : 0;
}

inline uint64_t fnv1a(uint64_t hash, const void * data, size_t size) {
    const auto * bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// One file per model file, context size and exact prefix tokens.
inline std::string snapshot_path(
    const std::string & directory,
    const std::string & model_path,
    int32_t n_ctx,
    const std::vector<llama_token> & tokens,
    size_t count
) {
    struct stat info {};
    if (directory.empty() || stat(model_path.c_str(), &info) != 0) return {};
    uint64_t hash = 14695981039346656037ull;
    const std::string salt = "agora-prefix-v1";
    hash = fnv1a(hash, salt.data(), salt.size());
    hash = fnv1a(hash, model_path.data(), model_path.size());
    const int64_t size = static_cast<int64_t>(info.st_size);
    const int64_t mtime = static_cast<int64_t>(info.st_mtime);
    hash = fnv1a(hash, &size, sizeof(size));
    hash = fnv1a(hash, &mtime, sizeof(mtime));
    hash = fnv1a(hash, &n_ctx, sizeof(n_ctx));
    hash = fnv1a(hash, tokens.data(), count * sizeof(llama_token));
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(hash));
    return directory + "/" + name + SNAPSHOT_SUFFIX;
}

inline bool file_exists(const std::string & path) {
    struct stat info {};
    return !path.empty() && stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// Replaces sequence 0 with the snapshot. On any mismatch the memory is left cleared.
inline bool restore(
    llama_context * ctx,
    const std::string & path,
    const std::vector<llama_token> & tokens,
    size_t count
) {
    llama_memory_t memory = llama_get_memory(ctx);
    llama_memory_clear(memory, true);
    std::vector<llama_token> stored(count);
    size_t stored_count = 0;
    const size_t read = llama_state_seq_load_file(
        ctx, path.c_str(), 0, stored.data(), stored.size(), &stored_count
    );
    const bool matches = read > 0 && stored_count == count &&
        std::equal(stored.begin(), stored.end(), tokens.begin()) &&
        // pos_min is not checked: a recurrent state only holds its last position.
        llama_memory_seq_pos_max(memory, 0) + 1 == static_cast<llama_pos>(count);
    if (!matches) {
        llama_memory_clear(memory, true);
        std::remove(path.c_str());
        return false;
    }
    utime(path.c_str(), nullptr);  // most recently used, for eviction
    return true;
}

inline void evict(const std::string & directory) {
    struct Entry { std::string path; time_t mtime; uint64_t size; };
    std::vector<Entry> entries;
    DIR * dir = opendir(directory.c_str());
    if (!dir) return;
    const size_t suffix_length = std::char_traits<char>::length(SNAPSHOT_SUFFIX);
    while (dirent * item = readdir(dir)) {
        const std::string name = item->d_name;
        if (name.size() <= suffix_length ||
            name.compare(name.size() - suffix_length, suffix_length, SNAPSHOT_SUFFIX) != 0) continue;
        const std::string path = directory + "/" + name;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0) continue;
        entries.push_back({path, info.st_mtime, static_cast<uint64_t>(info.st_size)});
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(),
              [](const Entry & a, const Entry & b) { return a.mtime > b.mtime; });
    uint64_t total = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        total += entries[i].size;
        if (i >= MAX_SNAPSHOT_FILES || (i > 0 && total > MAX_SNAPSHOT_BYTES)) {
            std::remove(entries[i].path.c_str());
        }
    }
}

// Call when sequence 0 holds exactly the first `count` prompt tokens.
inline bool save(
    llama_context * ctx,
    const std::string & directory,
    const std::string & path,
    const std::vector<llama_token> & tokens,
    size_t count
) {
    if (path.empty()) return false;
    mkdir(directory.c_str(), 0700);
    const std::string temporary = path + ".tmp";
    const size_t written = llama_state_seq_save_file(
        ctx, temporary.c_str(), 0, tokens.data(), count
    );
    if (written == 0 || std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    evict(directory);
    return true;
}

} // namespace agora::prefix_cache
