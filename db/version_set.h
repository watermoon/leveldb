// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// The representation of a DBImpl consists of a set of Versions.  The
// newest version is called "current".  Older versions may be kept
// around to provide a consistent view to live iterators.
//
// Each Version keeps track of a set of Table files per level.  The
// entire set of versions is maintained in a VersionSet.
//
// Version,VersionSet are thread-compatible, but require external
// synchronization on all accesses.

#ifndef STORAGE_LEVELDB_DB_VERSION_SET_H_
#define STORAGE_LEVELDB_DB_VERSION_SET_H_

#include <map>
#include <set>
#include <vector>

#include "db/dbformat.h"
#include "db/version_edit.h"
#include "port/port.h"
#include "port/thread_annotations.h"

namespace leveldb {

namespace log {
class Writer;
}

class Compaction;
class Iterator;
class MemTable;
class TableBuilder;
class TableCache;
class Version;
class VersionSet;
class WritableFile;

// Return the smallest index i such that files[i]->largest >= key.
// Return files.size() if there is no such file.
// REQUIRES: "files" contains a sorted list of non-overlapping files.
int FindFile(const InternalKeyComparator& icmp,
             const std::vector<FileMetaData*>& files, const Slice& key);

// Returns true iff some file in "files" overlaps the user key range
// [*smallest,*largest].
// smallest==nullptr represents a key smaller than all keys in the DB.
// largest==nullptr represents a key largest than all keys in the DB.
// REQUIRES: If disjoint_sorted_files, files[] contains disjoint ranges
//           in sorted order.
bool SomeFileOverlapsRange(const InternalKeyComparator& icmp,
                           bool disjoint_sorted_files,
                           const std::vector<FileMetaData*>& files,
                           const Slice* smallest_user_key,
                           const Slice* largest_user_key);

class Version {
 public:
  struct GetStats {
    FileMetaData* seek_file;
    int seek_file_level;
  };

  // Append to *iters a sequence of iterators that will
  // yield the contents of this Version when merged together.
  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  // 当融合在一起时, 追加一系列的迭代器到 *iters 来生成这个版本(Version)的内容
  // 要求: 这个版本已经保存 (看 VersionSet::SaveTo)
  void AddIterators(const ReadOptions&, std::vector<Iterator*>* iters);

  // Lookup the value for key.  If found, store it in *val and
  // return OK.  Else return a non-OK status.  Fills *stats.
  // REQUIRES: lock is not held
  // 查询 key 的值. 如果找到, 保存在 *val 并返回 OK. 否则范围非 OK. 填充 *stats
  // 要求: 没有持有锁
  Status Get(const ReadOptions&, const LookupKey& key, std::string* val,
             GetStats* stats);

  // Adds "stats" into the current state.  Returns true if a new
  // compaction may need to be triggered, false otherwise.
  // REQUIRES: lock is held
  // 将 "stats" 添加到当前状态. 如果一次新的压缩可能需要触发则返回 true, 否则 false
  // 要求: 持有锁
  bool UpdateStats(const GetStats& stats);

  // Record a sample of bytes read at the specified internal key.
  // Samples are taken approximately once every config::kReadBytesPeriod
  // bytes.  Returns true if a new compaction may need to be triggered.
  // REQUIRES: lock is held
  // 记录一次在指定内部 key 的读取中采样到的字节. 样本大约在每 config::kReadBytesPeriod
  // 字节进行. 如果一次新的压缩可能需要触发则返回 true
  // 要求: 持有锁
  bool RecordReadSample(Slice key);

  // Reference count management (so Versions do not disappear out from
  // under live iterators)
  // 引用计数管理(这样 Versions 摆脱了底层的 live 迭代器而不消失)
  void Ref();
  void Unref();

  void GetOverlappingInputs(
      int level,
      const InternalKey* begin,  // nullptr means before all keys
      const InternalKey* end,    // nullptr means after all keys
      std::vector<FileMetaData*>* inputs);

  // Returns true iff some file in the specified level overlaps
  // some part of [*smallest_user_key,*largest_user_key].
  // smallest_user_key==nullptr represents a key smaller than all the DB's keys.
  // largest_user_key==nullptr represents a key largest than all the DB's keys.
  // 当且仅当在指定的 level 中有一些文件和 [*smallest_user_key,*largest_user_key] 有
  // 交叠才返回 true.
  // smallest_user_key==nullptr 表示比 DB 所有 key 都小的 key
  // largest_user_key==nullptr 表示比 DB 所有 key 都大的 key
  bool OverlapInLevel(int level, const Slice* smallest_user_key,
                      const Slice* largest_user_key);

  // Return the level at which we should place a new memtable compaction
  // result that covers the range [smallest_user_key,largest_user_key].
  // 返回覆盖范围 [smallest_user_key,largest_user_key] 的 level, 用于告诉我们
  //  应该讲一个新的 memtable 压缩结果存放于哪一层
  int PickLevelForMemTableOutput(const Slice& smallest_user_key,
                                 const Slice& largest_user_key);

  int NumFiles(int level) const { return files_[level].size(); }

  // Return a human readable string that describes this version's contents.
  // 返回一个描述这个版本内容的人类可读字符串
  std::string DebugString() const;

 private:
  friend class Compaction;
  friend class VersionSet;

  class LevelFileNumIterator;

  explicit Version(VersionSet* vset)
      : vset_(vset),
        next_(this),
        prev_(this),
        refs_(0),
        file_to_compact_(nullptr),
        file_to_compact_level_(-1),
        compaction_score_(-1),
        compaction_level_(-1) {}

  Version(const Version&) = delete;
  Version& operator=(const Version&) = delete;

  ~Version();

  Iterator* NewConcatenatingIterator(const ReadOptions&, int level) const;

  // Call func(arg, level, f) for every file that overlaps user_key in
  // order from newest to oldest.  If an invocation of func returns
  // false, makes no more calls.
  //
  // REQUIRES: user portion of internal_key == user_key.
  // 对每一个和 user_key 交叠的文件按照从最新到最老的顺序调用 func(arg, level, f)
  // 如果其中一次 func 调用返回了 false, 则不再继续更多的调用
  void ForEachOverlapping(Slice user_key, Slice internal_key, void* arg,
                          bool (*func)(void*, int, FileMetaData*));

  VersionSet* vset_;  // VersionSet to which this Version belongs
  Version* next_;     // Next version in linked list
  Version* prev_;     // Previous version in linked list
  int refs_;          // Number of live refs to this version

  // List of files per level
  // 每一层的文件列表
  std::vector<FileMetaData*> files_[config::kNumLevels];

  // Next file to compact based on seek stats.
  // 基于查找状态的的下一个进行压缩的文件
  FileMetaData* file_to_compact_;
  int file_to_compact_level_;

  // Level that should be compacted next and its compaction score.
  // Score < 1 means compaction is not strictly needed.  These fields
  // are initialized by Finalize().
  // 下一个应该被压缩的 level, 以及它的压缩分数. Score < 1 表示压缩不是严格需要.
  // 这些字段被 Finalize() 初始化
  double compaction_score_;
  int compaction_level_;
};

class VersionSet {
 public:
  VersionSet(const std::string& dbname, const Options* options,
             TableCache* table_cache, const InternalKeyComparator*);
  VersionSet(const VersionSet&) = delete;
  VersionSet& operator=(const VersionSet&) = delete;

  ~VersionSet();

  // Apply *edit to the current version to form a new descriptor that
  // is both saved to persistent state and installed as the new
  // current version.  Will release *mu while actually writing to the file.
  // REQUIRES: *mu is held on entry.
  // REQUIRES: no other thread concurrently calls LogAndApply()
  // 将 *edit 应用到当前版本, 形成一个新描述符, 此描述符保存到持久状态并且作为新的
  // 当前版本. 实际写到文件后会释放 *mu
  // 要求: *mu 在进入(函数时)持有
  // 要求: 没有其他线程并发调用 LogAndApply()
  Status LogAndApply(VersionEdit* edit, port::Mutex* mu)
      EXCLUSIVE_LOCKS_REQUIRED(mu);

  // Recover the last saved descriptor from persistent storage.
  // 从持久化存储中恢复上一次保存的描述符
  Status Recover(bool* save_manifest);

  // Return the current version.
  // 返回当前版本
  Version* current() const { return current_; }

  // Return the current manifest file number
  // 放回当前 manifest 文件号码
  uint64_t ManifestFileNumber() const { return manifest_file_number_; }

  // Allocate and return a new file number
  // 分配并返回一个新的文件码
  uint64_t NewFileNumber() { return next_file_number_++; }

  // Arrange to reuse "file_number" unless a newer file number has
  // already been allocated.
  // REQUIRES: "file_number" was returned by a call to NewFileNumber().
  // 安排重用 "file_number" 除非一个新的文件码已经被分配到
  // 要求: "file_number" 是通过一个 NewFileNumber 的调用返回的
  void ReuseFileNumber(uint64_t file_number) {
    if (next_file_number_ == file_number + 1) {
      next_file_number_ = file_number;
    }
  }

  // Return the number of Table files at the specified level.
  // 返回指定层的 Table 数量
  int NumLevelFiles(int level) const;

  // Return the combined file size of all files at the specified level.
  // 返回指定层的所有文件大小之和
  int64_t NumLevelBytes(int level) const;

  // Return the last sequence number.
  // 返回上一个序列号
  uint64_t LastSequence() const { return last_sequence_; }

  // Set the last sequence number to s.
  // 设置上一个序列号为 s
  void SetLastSequence(uint64_t s) {
    assert(s >= last_sequence_);
    last_sequence_ = s;
  }

  // Mark the specified file number as used.
  // 标记指定的文件码为已使用
  void MarkFileNumberUsed(uint64_t number);

  // Return the current log file number.
  // 返回当前日志文件码
  uint64_t LogNumber() const { return log_number_; }

  // Return the log file number for the log file that is currently
  // being compacted, or zero if there is no such log file.
  // 返回当前正在被压缩的日志文件的文件码, 如果不存在这样的日志文件则返回 0
  uint64_t PrevLogNumber() const { return prev_log_number_; }

  // Pick level and inputs for a new compaction.
  // Returns nullptr if there is no compaction to be done.
  // Otherwise returns a pointer to a heap-allocated object that
  // describes the compaction.  Caller should delete the result.
  // 为一次新的压缩挑选 level 和输入.
  // 如果没有压缩会进行则返回 nullptr. 否则返回一个堆分配的用于描述压缩的
  // 对象的指针. 调用者应该删除结果.
  Compaction* PickCompaction();

  // Return a compaction object for compacting the range [begin,end] in
  // the specified level.  Returns nullptr if there is nothing in that
  // level that overlaps the specified range.  Caller should delete
  // the result.
  // 在指定的 level 返回一个压缩范围 [begin, end] 的压缩对象. 如果在那层没有
  // 任何东西和范围 [begin, end] 交叠则返回 nullptr. 调用者应该删除结果
  Compaction* CompactRange(int level, const InternalKey* begin,
                           const InternalKey* end);

  // Return the maximum overlapping data (in bytes) at next level for any
  // file at a level >= 1.
  // 返回对于 level >= 1 时下一层的任意文件交叠大小(以字节算)的最大值... 这翻译……
  // TODO: 重新翻译吧, 这翻译不忍直视
  int64_t MaxNextLevelOverlappingBytes();

  // Create an iterator that reads over the compaction inputs for "*c".
  // The caller should delete the iterator when no longer needed.
  // 创建一个读压缩输入 "*c" 的迭代器. 在不再需要的时候调用者应该删除迭代器
  Iterator* MakeInputIterator(Compaction* c);

  // Returns true iff some level needs a compaction.
  // 当且仅当某曾需要进行压缩时返回 true
  bool NeedsCompaction() const {
    Version* v = current_;
    return (v->compaction_score_ >= 1) || (v->file_to_compact_ != nullptr);
  }

  // Add all files listed in any live version to *live.
  // May also mutate some internal state.
  // 添加在任意活跃版本(live version)中列举的文件到 *live
  // 可能会修改一些内部状态
  void AddLiveFiles(std::set<uint64_t>* live);

  // Return the approximate offset in the database of the data for
  // "key" as of version "v".
  // 返回 "v" 版本的 "key" 的数据在数据库中的大致偏移
  uint64_t ApproximateOffsetOf(Version* v, const InternalKey& key);

  // Return a human-readable short (single-line) summary of the number
  // of files per level.  Uses *scratch as backing store.
  // 返回每层文件数的一个人类可读的短(一行)总结. 用 *scratch 作为底层存储
  struct LevelSummaryStorage {
    char buffer[100];
  };
  const char* LevelSummary(LevelSummaryStorage* scratch) const;

 private:
  class Builder;

  friend class Compaction;
  friend class Version;

  bool ReuseManifest(const std::string& dscname, const std::string& dscbase);

  void Finalize(Version* v);

  void GetRange(const std::vector<FileMetaData*>& inputs, InternalKey* smallest,
                InternalKey* largest);

  void GetRange2(const std::vector<FileMetaData*>& inputs1,
                 const std::vector<FileMetaData*>& inputs2,
                 InternalKey* smallest, InternalKey* largest);

  void SetupOtherInputs(Compaction* c);

  // Save current contents to *log
  Status WriteSnapshot(log::Writer* log);

  void AppendVersion(Version* v);

  Env* const env_;
  const std::string dbname_;
  const Options* const options_;
  TableCache* const table_cache_; // LRU
  const InternalKeyComparator icmp_;
  uint64_t next_file_number_;
  uint64_t manifest_file_number_;
  uint64_t last_sequence_;
  uint64_t log_number_;
  uint64_t prev_log_number_;  // 0 or backing store for memtable being compacted

  // Opened lazily
  WritableFile* descriptor_file_;
  log::Writer* descriptor_log_;
  Version dummy_versions_;  // Head of circular doubly-linked list of versions.
  Version* current_;        // == dummy_versions_.prev_

  // Per-level key at which the next compaction at that level should start.
  // Either an empty string, or a valid InternalKey.
  std::string compact_pointer_[config::kNumLevels];
};

// A Compaction encapsulates information about a compaction.
// 封装了一次压缩的信息
class Compaction {
 public:
  ~Compaction();

  // Return the level that is being compacted.  Inputs from "level"
  // and "level+1" will be merged to produce a set of "level+1" files.
  // 返回正在被压缩的层. "level" 和 ”level+1" 层的输入会被合并产生 "level+1" 层的文件
  int level() const { return level_; }

  // Return the object that holds the edits to the descriptor done
  // by this compaction.
  // 返回持有此压缩完成后的描述符的 edit 对象
  VersionEdit* edit() { return &edit_; }

  // "which" must be either 0 or 1
  // "which" 必须是 0 或者 1
  int num_input_files(int which) const { return inputs_[which].size(); }

  // Return the ith input file at "level()+which" ("which" must be 0 or 1).
  // 返回 "level()+which" 层的第 i 个输入文件
  FileMetaData* input(int which, int i) const { return inputs_[which][i]; }

  // Maximum size of files to build during this compaction.
  // 压缩过程中生成的最大文件大小
  uint64_t MaxOutputFileSize() const { return max_output_file_size_; }

  // Is this a trivial compaction that can be implemented by just
  // moving a single input file to the next level (no merging or splitting)
  // 这是否是一次可以仅仅通过移到一个单一的输入文件到下一层(没有发生合并和分裂)就可以
  // 完成的简单压缩
  bool IsTrivialMove() const;

  // Add all inputs to this compaction as delete operations to *edit.
  // 将此次压缩的所有输入以删除的操作添加到 *edit
  void AddInputDeletions(VersionEdit* edit);

  // Returns true if the information we have available guarantees that
  // the compaction is producing data in "level+1" for which no data exists
  // in levels greater than "level+1".
  // 当我们已知的信息可以保证在比此次压缩正在生成数据的 level+1 更高的层中没有数据时, 返回 true
  bool IsBaseLevelForKey(const Slice& user_key);

  // Returns true iff we should stop building the current output
  // before processing "internal_key".
  // 当且仅当我们应该在处理 "内部 key" 是应该停止构建当前输出
  bool ShouldStopBefore(const Slice& internal_key);

  // Release the input version for the compaction, once the compaction
  // is successful.
  // 一旦压缩成功, 释放此次压缩的输入版本
  void ReleaseInputs();

 private:
  friend class Version;
  friend class VersionSet;

  Compaction(const Options* options, int level);

  int level_;
  uint64_t max_output_file_size_;
  Version* input_version_;
  VersionEdit edit_;

  // Each compaction reads inputs from "level_" and "level_+1"
  // 每一次压缩从 "level_" 和 "level_+1" 层读取输入
  std::vector<FileMetaData*> inputs_[2];  // The two sets of inputs

  // State used to check for number of overlapping grandparent files
  // (parent == level_ + 1, grandparent == level_ + 2)
  // 用于检查祖父层级的文件中存在交叠 key 的文件数量的状态
  // (父层级 == level_ + 1, 祖父层级 == level_ + 2)
  std::vector<FileMetaData*> grandparents_;
  size_t grandparent_index_;  // Index in grandparent_starts_
  bool seen_key_;             // Some output key has been seen
  int64_t overlapped_bytes_;  // Bytes of overlap between current output
                              // and grandparent files

  // State for implementing IsBaseLevelForKey
  // 用于实现 IsBaseLevelForKey 的状态

  // level_ptrs_ holds indices into input_version_->levels_: our state
  // is that we are positioned at one of the file ranges for each
  // higher level than the ones involved in this compaction (i.e. for
  // all L >= level_ + 2).
  // level_ptrs_ 持有 input_version_->levels_ 的索引: 我们的状态是 --
  // 我们被放置在哪些比在此次压缩涉及的层级高的层集文件中的其中一个? 理解有误……
  size_t level_ptrs_[config::kNumLevels];
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_VERSION_SET_H_
