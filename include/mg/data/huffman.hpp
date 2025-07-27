// mg/data/huffman.hpp
#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include <mg/data/bitstream.hpp>

namespace mg::data {

template <typename T>
struct HuffmanNode {
  T value;
  uint32_t weight;
  HuffmanNode *parent = nullptr;
  HuffmanNode *child[2] = {nullptr, nullptr}; // Use raw pointers

  explicit HuffmanNode(T val = T()) : value(val), weight(0) {}

  // Encode the path from this node to the root
  std::pair<uint32_t, uint32_t> encode() const {
    if (!parent) {
      return {0, 0};
    }
    auto [val, size] = parent->encode();
    if (parent->child[0] == this) {
      return {val, size + 1};
    }
    return {val | (1 << size), size + 1};
  }
};

template <typename T>
class HuffmanTable {
public:
  explicit HuffmanTable(bool invert = false) : _invert(invert) {
    nodes.reserve(512); // Reserve space for nodes
  }

  // Add a leaf node with the specified value
  void addLeaf(T value) {
    nodes.push_back(std::make_unique<HuffmanNode<T>>(value));
  }

  // Get node by value (virtual, default implementation)
  virtual HuffmanNode<T> *getNode(T value) const {
    return nullptr; // Default: no direct access
  }

  // Build the Huffman tree up to max_size nodes
  void buildTree(size_t max_size) {
    // Calculate total weight and count non-zero weights
    uint32_t total_weight = 0;
    uint32_t non_zero_weights = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (!nodes[i]) {
        fprintf(stderr, "HuffmanTable: Null node at index %zu\n", i);
        return;
      }
      total_weight += nodes[i]->weight;
      if (nodes[i]->weight > 0) {
        ++non_zero_weights;
      }
    }

    // Handle edge cases
    if (non_zero_weights == 0) {
      fprintf(stderr, "HuffmanTable: No non-zero weights, tree not built.\n");
      return;
    }
    if (non_zero_weights == 1) {
      fprintf(stderr, "HuffmanTable: Single non-zero weight, "
                      "tree is trivial.\n");
      return;
    }

    // Build the tree by selecting two nodes with minimum weights
    while (nodes.size() < max_size) {
      HuffmanNode<T> *child0 = nullptr;
      HuffmanNode<T> *child1 = nullptr;
      size_t child0_idx = nodes.size();
      size_t child1_idx = nodes.size();
      uint32_t min_weight0 = std::numeric_limits<uint32_t>::max();
      uint32_t min_weight1 = std::numeric_limits<uint32_t>::max();

      for (size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i]) continue;
        if (nodes[i]->weight > 0 && !nodes[i]->parent) {
          if (nodes[i]->weight < min_weight0) {
            min_weight1 = min_weight0;
            child1 = child0;
            child1_idx = child0_idx;
            min_weight0 = nodes[i]->weight;
            child0 = nodes[i].get();
            child0_idx = i;
          } else if (nodes[i]->weight < min_weight1) {
            min_weight1 = nodes[i]->weight;
            child1 = nodes[i].get();
            child1_idx = i;
          }
        }
      }

      // Check if valid nodes were found
      if (!child0 || !child1 || child0_idx >= nodes.size() ||
                                child1_idx >= nodes.size()) {
        fprintf(stderr, "HuffmanTable: Not enough valid nodes.\n");
        break;
      }

      // Create parent node
      auto parent = std::make_unique<HuffmanNode<T>>();
      uint32_t parent_weight = child0->weight + child1->weight;
      parent->weight = parent_weight;

      // Assign existing nodes as children using raw pointers
      parent->child[_invert ? 1 : 0] = child0;
      parent->child[_invert ? 0 : 1] = child1;
      child0->parent = parent.get();
      child1->parent = parent.get();

      // Add parent to nodes list
      nodes.push_back(std::move(parent));

      // Count remaining active nodes
      size_t active_nodes = 0;
      for (size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i]) continue;
        if (nodes[i]->weight > 0 && !nodes[i]->parent) {
          ++active_nodes;
        }
      }

      if (active_nodes <= 1) {
        break;
      }
      if (parent_weight >= total_weight) {
        break;
      }
    }
  }

  // Decode a value from the bit stream
  T decodeSequence(BitStreamReader &stream) const {
    if (nodes.empty()) {
      fprintf(stderr, "Error: Empty Huffman table.\n");
      return T();
    }
    auto *node = nodes.back().get();
    if (!node) {
      fprintf(stderr, "Error: Invalid Huffman root node.\n");
      return T();
    }
    while (node->child[0] || node->child[1]) {
      uint8_t bit = stream.readBit();
      if (!node->child[bit]) {
        fprintf(stderr, "Error: Invalid Huffman path at bit %u\n", bit);
        return T();
      }
      node = node->child[bit];
      if (!node) {
        fprintf(stderr, "Error: Null node in Huffman path.\n");
        return T();
      }
    }
    return node->value;
  }

  // Encode a value to the bit stream
  void encodeValue(BitStreamWriter &writer, T value) const {
    // Try direct access via getNode
    if (auto *node = getNode(value)) {
      auto [val, size] = node->encode();
      writer.write(size, val);
      return;
    }
    // Fallback to iteration
    for (const auto &node : nodes) {
      if (node && node->value == value) {
        auto [val, size] = node->encode();
        writer.write(size, val);
        return;
      }
    }
    fprintf(stderr, "Error: No Huffman node for value=%u\n",
                    static_cast<unsigned>(value));
  }

  // Set the weight for a leaf node
  void setWeight(T value, uint32_t weight) {
    // Try direct access via getNode
    if (auto *node = getNode(value)) {
      node->weight = weight;
      return;
    }
    // Fallback to iteration
    for (auto &node : nodes) {
      if (node && node->value == value) {
        node->weight = weight;
        return;
      }
    }
    fprintf(stderr, "Error: No Huffman node for value=%u\n",
                    static_cast<unsigned>(value));
  }

protected:
  std::vector<std::unique_ptr<HuffmanNode<T>>> nodes;
  bool _invert;
};

class ByteHuffmanTable : public HuffmanTable<uint8_t> {
public:
  ByteHuffmanTable(bool invert = false) : HuffmanTable<uint8_t>(invert) {
    this->nodes.resize(256);
    for (int i = 0; i < 256; ++i) {
      this->nodes[i] =
        std::make_unique<HuffmanNode<uint8_t>>(static_cast<uint8_t>(i));
    }
  }

  // Get the node for a specific byte value
  HuffmanNode<uint8_t> *getNode(uint8_t value) const {
    return this->nodes[value].get();
  }
};

} // namespace mg::data
