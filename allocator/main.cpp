#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>

template <uint64_t MemorySize>
class SmartAllocator
{
  public:
    struct BlockInfo
    {
      uint64_t Id   = 0;
      void* Addr    = nullptr;
      uint64_t Size = 0;
    };

    SmartAllocator(const std::string& tag = std::string())
    {
      _blockUniqueId = 1;

      Reset();

      if (not tag.empty())
      {
        _tag = tag;

        printf("[SmartAllocator '%s']\n", _tag.data());
      }

      printf("Memory range: [0x%X - 0x%X]\n\n",
             &_memory[0], &_memory[MemorySize - 1]);
    }

    const BlockInfo& Alloc(uint64_t size)
    {
      if (_index + size >= MemorySize)
      {
        return _nullReference;
      }

      void* addr = &_memory[_index];
      _index += size;

      BlockInfo bi;
      bi.Id   = _blockUniqueId++;
      bi.Addr = addr;
      bi.Size = size;

      _blockInfoById[bi.Id] = bi;

      return _blockInfoById.at(bi.Id);
    };

    const BlockInfo& ReAlloc(const BlockInfo& bi, uint64_t size)
    {
      if (_index + size >= MemorySize)
      {
        return _nullReference;
      }

      if (_blockInfoById.count(bi.Id) != 1)
      {
        return _nullReference;
      }

      const BlockInfo& oldBlock = _blockInfoById.at(bi.Id);
      void* oldAddr = oldBlock.Addr;

      auto oldBlockSize = oldBlock.Size;

      void* newAddr = &_memory[_index];

      BlockInfo newBlock;
      newBlock.Id   = _blockUniqueId++;
      newBlock.Addr = newAddr;
      newBlock.Size = size;

      _blockInfoById[newBlock.Id] = newBlock;

      _index += size;

      std::memcpy(newAddr, oldAddr, oldBlockSize);

      Free(oldBlock);

      return _blockInfoById.at(newBlock.Id);
    };

    void Free(const BlockInfo& blockToFree)
    {
      if (_blockInfoById.count(blockToFree.Id) == 1)
      {
        const BlockInfo& bi = _blockInfoById.at(blockToFree.Id);

        std::memset(blockToFree.Addr, 0, bi.Size);
        //
        // We need to make "null reference",
        // otherwise even after erase old values will persist.
        //
        _blockInfoById[blockToFree.Id] = _nullReference;
        _blockInfoById.erase(blockToFree.Id);
      }
    };

    void Reset()
    {
      _blockInfoById.clear();
      std::memset(&_memory, 0, MemorySize);
      _index = 0;
    }

    void Defragment()
    {
      void* addr = &_memory;
      uint64_t index = 0;

      size_t blocks = _blockInfoById.size();

      for (size_t i = 0; i < blocks; i++)
      {
        auto it = _blockInfoById.begin();
        std::advance(it, i);

        BlockInfo& bi = it->second;
        std::memcpy(addr, bi.Addr, bi.Size);
        bi.Addr = addr;

        index += bi.Size;
        addr = &_memory[index];
      }

      _index = index;

      std::memset(addr, 0, MemorySize - index);
    }

  private:
    char _memory[MemorySize];
    uint64_t _index = 0;

    std::map<uint64_t, BlockInfo> _blockInfoById;

    const BlockInfo _nullReference = { 0, nullptr, 0 };

    std::string _tag;

    uint64_t _blockUniqueId = 0;
};

// =============================================================================

class SmallAllocator
{
  public:
    SmallAllocator()
    {
      Reset();

      /*
      printf("Memory range: [0x%X - 0x%X]\n\n",
             &_memory[0], &_memory[MemorySize - 1]);
      */
    }

    void* Alloc(uint64_t size)
    {
      if (_index + size >= MemorySize)
      {
        return nullptr;
      }

      void* addr = &_memory[_index];
      _blockSizeByAddr[addr] = size;
      _index += size;
      return addr;
    };

    void* ReAlloc(void* ptr, uint64_t size)
    {
      if (_index + size >= MemorySize)
      {
        return nullptr;
      }

      auto oldBlockSize = _blockSizeByAddr[ptr];

      void* newAddr = &_memory[_index];
      _blockSizeByAddr[newAddr] = size;
      _index += size;

      std::memcpy(newAddr, ptr, oldBlockSize);

      Free(ptr);

      return newAddr;
    };

    void Free(void* ptr)
    {
      if (_blockSizeByAddr.count(ptr) == 1)
      {
        auto blockSize = _blockSizeByAddr[ptr];
        std::memset(ptr, 0, blockSize);
        _blockSizeByAddr.erase(ptr);
      }
    };

    void Reset()
    {
      _blockSizeByAddr.clear();
      std::memset(&_memory, 0, MemorySize);
      _index = 0;
    }

    void Defragment()
    {
      void* addr = &_memory;
      uint64_t index = 0;

      std::map<void*, uint64_t> newLayout;

      while (not _blockSizeByAddr.empty())
      {
        auto it = _blockSizeByAddr.begin();
        std::memcpy(addr, it->first, it->second);
        index += it->second;
        addr = &_memory[index];
        newLayout[addr] = it->second;
        _blockSizeByAddr.erase(it);
      }

      std::memset(addr, 0, MemorySize - index);

      _blockSizeByAddr = newLayout;
    }

  private:
    static const uint64_t MemorySize = 32;

    char _memory[MemorySize];
    uint64_t _index = 0;
    std::map<void*, uint64_t> _blockSizeByAddr;
};

// =============================================================================

void FillBuffer(char* begin, size_t size)
{
  for (size_t i = 0; i < size; i++)
  {
    begin[i] = (i == size - 1) ? 255 : i;
  }
}

void FillBuffer(char* begin, size_t size, char value)
{
  for (size_t i = 0; i < size; i++)
  {
    begin[i] = (i == size - 1) ? 255 : value;
  }
}

// =============================================================================

int main()
{
  const size_t blockSize = sizeof(int);

  SmallAllocator a;

  char* p1 = (char*)a.Alloc(blockSize);
  FillBuffer(p1, blockSize);
  char* p2 = (char*)a.Alloc(4 * blockSize);
  FillBuffer(p2, 4 * blockSize);
  char* p3 = (char*)a.Alloc(2 * blockSize);
  FillBuffer(p3, 2 * blockSize);

  a.Free(p2);

  //
  // FIXME: all raw pointers to previously allocated memory
  // are now invalid!
  //
  a.Defragment();

  SmartAllocator<64> sa("SmartAlloc1");

  const auto& sp1 = sa.Alloc(blockSize);
  FillBuffer((char*)sp1.Addr, sp1.Size);
  const auto& sp2 = sa.Alloc(4 * blockSize);
  FillBuffer((char*)sp2.Addr, sp2.Size);
  const auto& sp3 = sa.Alloc(2 * blockSize);
  FillBuffer((char*)sp3.Addr, sp3.Size);

  sa.Free(sp2);

  sa.Defragment();

  //
  // sp2 is invalid, but this won't crash the program
  // since now it just contains a placeholder value
  // of 0 size buffer, so for loop won't even start.
  //
  // In reality one should check BlockInfo::Addr against nullptr.
  //
  FillBuffer((char*)sp2.Addr, sp2.Size, 32);

  //
  // Now works!
  //
  FillBuffer((char*)sp3.Addr, sp3.Size, 64);

  //
  // Still, after realloc old reference (sp3) will be invalidated.
  //
  const auto& sp4 = sa.ReAlloc(sp3, 3 * blockSize);
  FillBuffer((char*)sp4.Addr, sp4.Size);

  const auto& sp5 = sa.Alloc(3 * blockSize);
  FillBuffer((char*)sp5.Addr, sp5.Size);

  sa.Free(sp4);

  sa.Defragment();

  /*
  SmallAllocator A1;
  int * A1_P1 = (int *) A1.Alloc(sizeof(int));
  A1_P1 = (int *) A1.ReAlloc(A1_P1, 2 * sizeof(int));
  A1.Free(A1_P1);
  SmallAllocator A2;
  int * A2_P1 = (int *) A2.Alloc(10 * sizeof(int));
  for(unsigned int i = 0; i < 10; i++) A2_P1[i] = i;
  for(unsigned int i = 0; i < 10; i++) if(A2_P1[i] != i) std::cout << "ERROR 1" << std::endl;
  int * A2_P2 = (int *) A2.Alloc(10 * sizeof(int));
  for(unsigned int i = 0; i < 10; i++) A2_P2[i] = -1;
  for(unsigned int i = 0; i < 10; i++) if(A2_P1[i] != i) std::cout << "ERROR 2" << std::endl;
  for(unsigned int i = 0; i < 10; i++) if(A2_P2[i] != -1) std::cout << "ERROR 3" << std::endl;
  A2_P1 = (int *) A2.ReAlloc(A2_P1, 20 * sizeof(int));
  for(unsigned int i = 10; i < 20; i++) A2_P1[i] = i;
  for(unsigned int i = 0; i < 20; i++) if(A2_P1[i] != i) std::cout << "ERROR 4" << std::endl;
  for(unsigned int i = 0; i < 10; i++) if(A2_P2[i] != -1) std::cout << "ERROR 5" << std::endl;
  A2_P1 = (int *) A2.ReAlloc(A2_P1, 5 * sizeof(int));
  for(unsigned int i = 0; i < 5; i++) if(A2_P1[i] != i) std::cout << "ERROR 6" << std::endl;
  for(unsigned int i = 0; i < 10; i++) if(A2_P2[i] != -1) std::cout << "ERROR 7" << std::endl;
  A2.Free(A2_P1);
  A2.Free(A2_P2);
  */

  printf("All done!\n");

  return 0;
}
