#include "obelisk/Runtime/ActivationOrder.h"
#include "gtest/gtest.h"

#include <numeric>
#include <random>

using obelisk::runtime::ActivationOrder;
using Edge = std::pair<uint32_t, uint32_t>;

TEST(ActivationOrder, EmptyAndInvalidGraphs) {
  ActivationOrder order;
  ASSERT_TRUE(ActivationOrder::build(0, {}, order));
  EXPECT_TRUE(order.nodes.empty());
  EXPECT_EQ(order.componentOffsets, std::vector<uint32_t>{0});
  ASSERT_TRUE(ActivationOrder::build(1, {}, order));
  EXPECT_FALSE(ActivationOrder::build(1, {{0, 1}}, order));
  EXPECT_EQ(order.nodes, std::vector<uint32_t>{0});
}

TEST(ActivationOrder, DiamondAndIndependentCone) {
  ActivationOrder order;
  ASSERT_TRUE(ActivationOrder::build(
      6, {{5, 3}, {5, 1}, {3, 0}, {1, 0}, {4, 2}, {5, 1}}, order));
  EXPECT_EQ(order.nodes, (std::vector<uint32_t>{4, 2, 5, 1, 3, 0}));
  EXPECT_EQ(order.componentOffsets.size(), 7u);
}

TEST(ActivationOrder, PotentialRoutingCycleStaysLocal) {
  ActivationOrder possible, configured, changed;
  std::vector<Edge> edges{{0, 1}, {1, 2}, {2, 1}, {2, 3}, {4, 5}};
  ASSERT_TRUE(ActivationOrder::build(6, edges, possible));
  EXPECT_EQ(possible.componentOf[1], possible.componentOf[2]);
  EXPECT_NE(possible.componentOf[0], possible.componentOf[1]);
  EXPECT_NE(possible.componentOf[3], possible.componentOf[1]);
  EXPECT_NE(possible.componentOf[4], possible.componentOf[1]);
  // An external proof of the selector, not a last-value observation, permits
  // removing the disabled route. Changing that selector restores its edge.
  edges.erase(edges.begin() + 2);
  ASSERT_TRUE(ActivationOrder::build(6, edges, configured));
  EXPECT_NE(configured.componentOf[1], configured.componentOf[2]);
  edges.emplace_back(2, 1);
  ASSERT_TRUE(ActivationOrder::build(6, edges, changed));
  EXPECT_EQ(changed.nodes, possible.nodes);
  EXPECT_EQ(changed.componentOf, possible.componentOf);
}

TEST(ActivationOrder, LargeGraphUsesNoRecursiveTraversalOrOwnerWordLimit) {
  constexpr uint32_t count = 100000;
  std::vector<Edge> edges;
  for (uint32_t node = 1; node < count; ++node)
    edges.emplace_back(node, node - 1);
  ActivationOrder order;
  ASSERT_TRUE(ActivationOrder::build(count, edges, order));
  EXPECT_EQ(order.nodes.front(), count - 1);
  EXPECT_EQ(order.nodes.back(), 0u);
  EXPECT_EQ(order.componentOffsets.size(), count + 1u);
  edges.emplace_back(0, count - 1);
  ASSERT_TRUE(ActivationOrder::build(count, std::move(edges), order));
  EXPECT_EQ(order.componentOffsets, (std::vector<uint32_t>{0, count}));
}

TEST(ActivationOrder, DependencyTraversalInsidePotentialCycle) {
  // Owner identity is deliberately unrelated to the route. Sorting these
  // identities gives four backward edges; dependency traversal gives one.
  // The SCC remains intact, so a configured route or an actual oscillator
  // still publishes work through the retained closing edge.
  std::vector<Edge> edges{{5, 3}, {3, 1}, {1, 4}, {4, 2}, {2, 0}, {0, 5}};
  ActivationOrder order;
  ASSERT_TRUE(ActivationOrder::build(6, edges, order));
  EXPECT_EQ(order.componentOffsets, (std::vector<uint32_t>{0, 6}));
  std::vector<uint32_t> rank(6);
  for (uint32_t i = 0; i < order.nodes.size(); ++i)
    rank[order.nodes[i]] = i;
  unsigned backward = 0;
  for (auto [from, to] : edges)
    backward += rank[from] >= rank[to];
  EXPECT_EQ(backward, 1u);
  std::reverse(edges.begin(), edges.end());
  ActivationOrder reversed;
  ASSERT_TRUE(ActivationOrder::build(6, edges, reversed));
  EXPECT_EQ(order.nodes, reversed.nodes);
}

TEST(ActivationOrder, RandomGraphsAgreeWithScalarReachability) {
  std::mt19937 random(1830043);
  for (unsigned trial = 0; trial != 400; ++trial) {
    uint32_t count = 1 + random() % 24;
    std::vector<Edge> edges;
    std::vector<std::vector<bool>> reachable(count, std::vector<bool>(count));
    for (uint32_t from = 0; from < count; ++from) {
      reachable[from][from] = true;
      for (uint32_t to = 0; to < count; ++to)
        if (random() % 9 == 0) {
          edges.emplace_back(from, to);
          reachable[from][to] = true;
        }
    }
    for (uint32_t through = 0; through < count; ++through)
      for (uint32_t from = 0; from < count; ++from)
        for (uint32_t to = 0; to < count; ++to)
          reachable[from][to] =
              reachable[from][to] ||
              (reachable[from][through] && reachable[through][to]);
    ActivationOrder order, shuffled;
    ASSERT_TRUE(ActivationOrder::build(count, edges, order));
    auto identities = order.nodes;
    std::sort(identities.begin(), identities.end());
    for (uint32_t node = 0; node < count; ++node)
      EXPECT_EQ(identities[node], node);
    for (uint32_t from = 0; from < count; ++from)
      for (uint32_t to = 0; to < count; ++to) {
        EXPECT_EQ(order.componentOf[from] == order.componentOf[to],
                  reachable[from][to] && reachable[to][from]);
        if (reachable[from][to]) {
          EXPECT_LE(order.componentOf[from], order.componentOf[to]);
        }
      }
    std::shuffle(edges.begin(), edges.end(), random);
    ASSERT_TRUE(ActivationOrder::build(count, edges, shuffled));
    EXPECT_EQ(order.nodes, shuffled.nodes);
    EXPECT_EQ(order.componentOf, shuffled.componentOf);
  }
}
