/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "cluster/cluster.h"

#include <fmt/core.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "cluster/cluster_defs.h"
#include "commands/commander.h"
#include "server/server.h"
#include "test_base.h"

class ClusterTest : public TestBase {
 protected:
  explicit ClusterTest() = default;
  ~ClusterTest() override = default;
};

TEST_F(ClusterTest, CluseterSetNodes) {
  Status s;

  auto config = storage_->GetConfig();
  // don't start workers
  config->workers = 0;
  Server server(storage_.get(), config);
  // we don't need the server resource, so just stop it once it's started
  server.Stop();
  server.Join();

  Cluster cluster(&server, {"127.0.0.1"}, 3002);

  const std::string invalid_fields =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave";
  s = cluster.SetClusterNodes(invalid_fields, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster nodes info");

  const std::string invalid_node_id =
      "07c37dfeb235213a872192d90877d0cd55635b9 127.0.0.1 30004 "
      "slave 07c37dfeb235213a872192d90877d0cd55635b92";
  s = cluster.SetClusterNodes(invalid_node_id, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node id");

  const std::string invalid_port =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 unknown "
      "master 07c37dfeb235213a872192d90877d0cd55635b91 5461-10922";
  s = cluster.SetClusterNodes(invalid_port, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node port");

  const std::string slave_has_no_master =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave -";
  s = cluster.SetClusterNodes(slave_has_no_master, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node id");

  const std::string master_has_master =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master 07c37dfeb235213a872192d90877d0cd55635b91 5461-10922";
  s = cluster.SetClusterNodes(master_has_master, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node id");

  const std::string invalid_slot_range =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-0";
  s = cluster.SetClusterNodes(invalid_slot_range, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Slot is out of range");

  const std::string invalid_slot_id =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 54610";
  s = cluster.SetClusterNodes(invalid_slot_id, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Slot is out of range");

  const std::string overlapped_slot_id =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 0-126\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa2 127.0.0.1 30003 "
      "master - 0-16383";
  s = cluster.SetClusterNodes(overlapped_slot_id, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Slot distribution is overlapped");

  const std::string right_nodes =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 0 123-456 789 831 8192-16381 16382 16383";
  s = cluster.SetClusterNodes(right_nodes, 1, false);
  ASSERT_TRUE(s.IsOK());
  ASSERT_TRUE(cluster.GetVersion() == 1);
}

TEST_F(ClusterTest, CluseterGetNodes) {
  const std::string nodes =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave e7d1eecce10fd6bb5eb35b9f99a514335d9ba9ca\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-10922";
  auto config = storage_->GetConfig();
  // don't start workers
  config->workers = 0;
  Server server(storage_.get(), config);
  // we don't need the server resource, so just stop it once it's started
  server.Stop();
  server.Join();

  Cluster cluster(&server, {"127.0.0.1"}, 30002);
  Status s = cluster.SetClusterNodes(nodes, 1, false);
  ASSERT_TRUE(s.IsOK());

  std::string output_nodes;
  s = cluster.GetClusterNodes(&output_nodes);
  ASSERT_TRUE(s.IsOK());

  std::vector<std::string> vnodes = util::Split(output_nodes, "\n");

  for (const auto &vnode : vnodes) {
    std::vector<std::string> node_fields = util::Split(vnode, " ");

    if (node_fields[0] == "07c37dfeb235213a872192d90877d0cd55635b91") {
      ASSERT_TRUE(node_fields.size() == 8);
      ASSERT_TRUE(node_fields[1] == "127.0.0.1:30004@40004");
      ASSERT_TRUE(node_fields[2] == "slave");
      ASSERT_TRUE(node_fields[3] == "e7d1eecce10fd6bb5eb35b9f99a514335d9ba9ca");
      ASSERT_TRUE(node_fields[6] == "1");
      ASSERT_TRUE(node_fields[7] == "connected");
    } else {
      ASSERT_TRUE(node_fields.size() == 9);
      ASSERT_TRUE(node_fields[1] == "127.0.0.1:30002@40002");
      ASSERT_TRUE(node_fields[2] == "myself,master");
      ASSERT_TRUE(node_fields[3] == "-");
      ASSERT_TRUE(node_fields[6] == "1");
      ASSERT_TRUE(node_fields[7] == "connected");
      ASSERT_TRUE(node_fields[8] == "5461-10922");
    }
  }
}

TEST_F(ClusterTest, CluseterGetSlotInfo) {
  const std::string nodes =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-10922";

  auto config = storage_->GetConfig();
  // don't start workers
  config->workers = 0;
  Server server(storage_.get(), config);
  // we don't need the server resource, so just stop it once it's started
  server.Stop();
  server.Join();

  Cluster cluster(&server, {"127.0.0.1"}, 30002);
  Status s = cluster.SetClusterNodes(nodes, 1, false);
  ASSERT_TRUE(s.IsOK());

  std::vector<SlotInfo> slots_infos;
  s = cluster.GetSlotsInfo(&slots_infos);
  ASSERT_TRUE(s.IsOK());
  ASSERT_TRUE(slots_infos.size() == 1);
  SlotInfo info = slots_infos[0];
  ASSERT_TRUE(info.start == 5461);
  ASSERT_TRUE(info.end == 10922);
  ASSERT_TRUE(info.nodes.size() == 2);
  ASSERT_TRUE(info.nodes[0].port == 30002);
  ASSERT_TRUE(info.nodes[1].id == "07c37dfeb235213a872192d90877d0cd55635b91");
}

TEST_F(ClusterTest, TestDumpAndLoadClusterNodesInfo) {
  int64_t version = 2;
  const std::string nodes =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-10922\n"
      "17ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30003 master - 10923-16383";

  auto config = storage_->GetConfig();
  // don't start workers
  config->workers = 0;
  Server server(storage_.get(), config);
  // we don't need the server resource, so just stop it once it's started
  server.Stop();
  server.Join();

  Cluster cluster(&server, {"127.0.0.1"}, 30002);
  Status s = cluster.SetClusterNodes(nodes, version, false);
  ASSERT_TRUE(s.IsOK());

  std::string nodes_filename = "nodes.conf";
  s = cluster.DumpClusterNodes(nodes_filename);
  ASSERT_TRUE(s.IsOK());
  Cluster new_cluster(nullptr, {"127.0.0.1"}, 30002);
  s = new_cluster.LoadClusterNodes(nodes_filename);
  ASSERT_TRUE(s.IsOK());
  ASSERT_EQ(version, new_cluster.GetVersion());
  std::vector<SlotInfo> slots_infos;
  s = new_cluster.GetSlotsInfo(&slots_infos);
  ASSERT_TRUE(s.IsOK());
  ASSERT_EQ(2, slots_infos.size());
  SlotInfo slot0_info = slots_infos[0];
  ASSERT_EQ(5461, slot0_info.start);
  ASSERT_EQ(10922, slot0_info.end);
  ASSERT_EQ(2, slot0_info.nodes.size());
  ASSERT_EQ(30002, slot0_info.nodes[0].port);
  ASSERT_EQ("07c37dfeb235213a872192d90877d0cd55635b91", slot0_info.nodes[1].id);
  SlotInfo slot1_info = slots_infos[1];
  ASSERT_EQ(10923, slot1_info.start);
  ASSERT_EQ(16383, slot1_info.end);
  ASSERT_EQ(1, slot1_info.nodes.size());
  ASSERT_EQ(30003, slot1_info.nodes[0].port);
  ASSERT_EQ("17ed2db8d677e59ec4a4cefb06858cf2a1a89fa1", slot1_info.nodes[0].id);

  unlink(nodes_filename.c_str());
}

TEST_F(ClusterTest, ClusterParseSlotRanges) {
  Status s;

  auto config = storage_->GetConfig();
  // don't start workers
  config->workers = 0;
  Server server(storage_.get(), config);
  // we don't need the server resource, so just stop it once it's started
  server.Stop();
  server.Join();

  Cluster cluster(&server, {"127.0.0.1"}, 3002);
  const std::string node_id = "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1";
  int64_t version = 1;

  const std::string right_nodes = node_id +
                                  " 127.0.0.1 30002 "
                                  "master - 0 123-456 789 831 8192-16381 16382 16383";
  s = cluster.SetClusterNodes(right_nodes, version, false);
  ASSERT_TRUE(s.IsOK());
  ASSERT_TRUE(cluster.GetVersion() == version);
  version++;

  std::vector<SlotRange> slots;

  const std::string t_single_slot = "1234";
  s = redis::CommandTable::ParseSlotRanges(t_single_slot, slots);
  ASSERT_TRUE(s.IsOK());
  s = cluster.SetSlotRanges(slots, node_id, version);
  ASSERT_TRUE(s.IsOK());
  version++;
  slots.clear();

  const std::string t_single_ranges = "1234-1236";
  s = redis::CommandTable::ParseSlotRanges(t_single_ranges, slots);
  ASSERT_TRUE(s.IsOK());
  s = cluster.SetSlotRanges(slots, node_id, version);
  ASSERT_TRUE(s.IsOK());
  version++;
  slots.clear();

  const std::string t_mixed_slot = "10229  16301 4710 3557-8559 ";
  s = redis::CommandTable::ParseSlotRanges(t_mixed_slot, slots);
  ASSERT_TRUE(s.IsOK());
  s = cluster.SetSlotRanges(slots, node_id, version);
  ASSERT_TRUE(s.IsOK());
  version++;
  slots.clear();

  std::string empty_slots;
  s = redis::CommandTable::ParseSlotRanges(empty_slots, slots);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "No slots to parse.");
  slots.clear();

  std::string space_slots = "    ";
  s = redis::CommandTable::ParseSlotRanges(space_slots, slots);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == fmt::format("Invalid slots: `{}`. No slots to parse. "
                                     "Please use spaces to separate slots.",
                                     space_slots));

  std::vector<std::string> error_slots;
  std::string invalid_single_slot = "830849ad";
  std::string unbound_single_slot = "1683093429";
  std::string front_slot_ranges = "-1234-3456";
  std::string back_slot_ranges = "1234-3456-";
  std::string f_single_slot = "-6351";
  std::string overmuch_slot_ranges = "12-34-56";
  std::string f_cond_slot_ranges = "3456-1234";

  error_slots.emplace_back(invalid_single_slot);
  error_slots.emplace_back(unbound_single_slot);
  error_slots.emplace_back(front_slot_ranges);
  error_slots.emplace_back(back_slot_ranges);
  error_slots.emplace_back(f_single_slot);
  error_slots.emplace_back(overmuch_slot_ranges);
  error_slots.emplace_back(f_cond_slot_ranges);

  slots.clear();
  for (int i = 0; i < 2; i++) {
    if (i == 1) {
      for (auto &slot_str : error_slots) {
        slot_str = t_mixed_slot + slot_str;
      }
    }

    s = redis::CommandTable::ParseSlotRanges(error_slots[0], slots);
    ASSERT_FALSE(s.IsOK());
    ASSERT_TRUE(s.Msg() == "Invalid slot id: encounter non-integer characters");
    slots.clear();

    s = redis::CommandTable::ParseSlotRanges(error_slots[1], slots);
    ASSERT_FALSE(s.IsOK());
    ASSERT_TRUE(s.Msg() == "Invalid slot id: out of numeric range");
    slots.clear();

    s = redis::CommandTable::ParseSlotRanges(error_slots[2], slots);
    ASSERT_FALSE(s.IsOK());
    ASSERT_TRUE(s.Msg() == fmt::format("Invalid slot range: `{}`. The character '-' can't appear "
                                       "in the first or last position.",
                                       front_slot_ranges));
    slots.clear();

    s = redis::CommandTable::ParseSlotRanges(error_slots[3], slots);
    ASSERT_FALSE(s.IsOK());
    ASSERT_TRUE(s.Msg() ==
                fmt::format("Invalid slot range: `{}`. The character '-' can't appear in the first or last position.",
                            back_slot_ranges));
    slots.clear();

    s = redis::CommandTable::ParseSlotRanges(error_slots[4], slots);
    ASSERT_FALSE(s.IsOK());
    ASSERT_TRUE(s.Msg() ==
                fmt::format("Invalid slot range: `{}`. The character '-' can't appear in the first or last position.",
                            f_single_slot));
    slots.clear();

    s = redis::CommandTable::ParseSlotRanges(error_slots[5], slots);
    ASSERT_FALSE(s.IsOK());
    ASSERT_TRUE(s.Msg() == fmt::format("Invalid slot range: `{}`. The slot range should be of the form `int1-int2`.",
                                       overmuch_slot_ranges));
    slots.clear();

    s = redis::CommandTable::ParseSlotRanges(error_slots[6], slots);
    ASSERT_FALSE(s.IsOK());
    ASSERT_TRUE(
        s.Msg() ==
        fmt::format(
            "Invalid slot range: `{}`. The slot range `int1-int2` needs to satisfy the condition (int1 <= int2).",
            f_cond_slot_ranges));
    slots.clear();
  }
}

TEST_F(ClusterTest, GetReplicas) {
  auto config = storage_->GetConfig();
  // don't start workers
  config->workers = 0;
  Server server(storage_.get(), config);
  // we don't need the server resource, so just stop it once it's started
  server.Stop();
  server.Join();

  const std::string nodes =
      "7dbee3d628f04cc5d763b36e92b10533e627a1d0 127.0.0.1 6480 slave 159dde1194ebf5bfc5a293dff839c3d1476f2a49\n"
      "159dde1194ebf5bfc5a293dff839c3d1476f2a49 127.0.0.1 6479 master - 8192-16383\n"
      "bb2e5b3c5282086df51eff6b3e35519aede96fa6 127.0.0.1 6379 master - 0-8191";

  Cluster cluster(&server, {"127.0.0.1"}, 6379);
  Status s = cluster.SetClusterNodes(nodes, 2, false);
  ASSERT_TRUE(s.IsOK());

  auto with_replica = cluster.GetReplicas("159dde1194ebf5bfc5a293dff839c3d1476f2a49");
  ASSERT_TRUE(s.IsOK());

  std::vector<std::string> replicas = util::Split(with_replica.GetValue(), "\n");
  for (const auto &replica : replicas) {
    std::vector<std::string> replica_fields = util::Split(replica, " ");

    ASSERT_TRUE(replica_fields.size() == 8);
    ASSERT_TRUE(replica_fields[0] == "7dbee3d628f04cc5d763b36e92b10533e627a1d0");
    ASSERT_TRUE(replica_fields[1] == "127.0.0.1:6480@16480");
    ASSERT_TRUE(replica_fields[2] == "slave");
    ASSERT_TRUE(replica_fields[3] == "159dde1194ebf5bfc5a293dff839c3d1476f2a49");
    ASSERT_TRUE(replica_fields[7] == "connected");
  }

  auto without_replica = cluster.GetReplicas("bb2e5b3c5282086df51eff6b3e35519aede96fa6");
  ASSERT_TRUE(without_replica.IsOK());
  ASSERT_EQ(without_replica.GetValue(), "");

  auto replica_node = cluster.GetReplicas("7dbee3d628f04cc5d763b36e92b10533e627a1d0");
  ASSERT_FALSE(replica_node.IsOK());
  ASSERT_EQ(replica_node.Msg(), "The node isn't a master");

  auto unknown_node = cluster.GetReplicas("1234567890");
  ASSERT_FALSE(unknown_node.IsOK());
  ASSERT_EQ(unknown_node.Msg(), "Invalid cluster node id");
}

// Test LB address functionality
TEST_F(ClusterTest, ClusterNodeLBAddress) {
  // Test constructor without LB address
  std::bitset<kClusterSlots> slots;
  slots.set(0, true);
  ClusterNode node1("67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1", "192.168.1.10", 6379, 
                   kClusterMaster, "-", slots);
  
  ASSERT_EQ(node1.GetNodeIP(), "192.168.1.10");
  ASSERT_EQ(node1.GetNodePort(), 6379);
  
  // Test constructor with LB address
  ClusterNode node2("67ed2db8d677e59ec4a4cefb06858cf2a1a89fa2", "192.168.1.11", 6379, 
                   kClusterMaster, "-", slots, "10.0.0.100", 6379);
  
  ASSERT_EQ(node2.host, "192.168.1.11");  // Real IP
  ASSERT_EQ(node2.port, 6379);            // Real port
  ASSERT_EQ(node2.lb_ip, "10.0.0.100");   // LB IP
  ASSERT_EQ(node2.lb_port, 6379);         // LB port
  ASSERT_EQ(node2.GetNodeIP(), "10.0.0.100");   // Display LB IP
  ASSERT_EQ(node2.GetNodePort(), 6379);         // Display LB port
  
  // Test empty LB address
  ClusterNode node3("67ed2db8d677e59ec4a4cefb06858cf2a1a89fa3", "192.168.1.12", 6379, 
                   kClusterMaster, "-", slots, "", 0);
  
  ASSERT_EQ(node3.GetNodeIP(), "192.168.1.12");  // Fallback to real IP
  ASSERT_EQ(node3.GetNodePort(), 6379);          // Fallback to real port
}

TEST_F(ClusterTest, ClusterSetNodesWithLBAddress) {
  Status s;
  auto config = storage_->GetConfig();
  config->workers = 0;
  Server server(storage_.get(), config);
  server.Stop();
  server.Join();
  
  Cluster cluster(&server, {"127.0.0.1"}, 3002);

  // Test nodes with LB address at the end
  const std::string nodes_with_lb =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 192.168.1.10 6379 "
      "master - 0-5000 10.0.0.100 6379\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa2 192.168.1.11 6379 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 10.0.0.101 6379";
  
  s = cluster.SetClusterNodes(nodes_with_lb, 1, false);
  ASSERT_TRUE(s.IsOK());
  
  // Test mixed format (some nodes with LB, some without)
  const std::string mixed_nodes =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 192.168.1.10 6379 "
      "master - 0-5000 10.0.0.100 6379\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa2 192.168.1.11 6379 "
      "master - 5001-10000";  // No LB address
  
  s = cluster.SetClusterNodes(mixed_nodes, 2, false);
  ASSERT_TRUE(s.IsOK());
  
  // Test backward compatibility (old format)
  const std::string old_format_nodes =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 0-5000\n"
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1";
  
  s = cluster.SetClusterNodes(old_format_nodes, 3, false);
  ASSERT_TRUE(s.IsOK());
}

TEST_F(ClusterTest, ClusterNodesOutputWithLBAddress) {
  const std::string nodes_with_lb =
      "07c37dfeb235213a872192d90877d0cd55635b91 192.168.1.11 30004 "
      "slave e7d1eecce10fd6bb5eb35b9f99a514335d9ba9ca 10.0.0.101 30004\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 192.168.1.10 30002 "
      "master - 5461-10922 10.0.0.100 30002";
  
  auto config = storage_->GetConfig();
  config->workers = 0;
  Server server(storage_.get(), config);
  server.Stop();
  server.Join();
  
  Cluster cluster(&server, {"192.168.1.10"}, 30002);
  Status s = cluster.SetClusterNodes(nodes_with_lb, 1, false);
  ASSERT_TRUE(s.IsOK());
  
  std::string output_nodes;
  s = cluster.GetClusterNodes(&output_nodes);
  ASSERT_TRUE(s.IsOK());
  
  // Verify output uses LB addresses
  ASSERT_TRUE(output_nodes.find("10.0.0.100:30002@40002") != std::string::npos);
  ASSERT_TRUE(output_nodes.find("10.0.0.101:30004@40004") != std::string::npos);
  
  // Verify real IPs are not in output
  ASSERT_TRUE(output_nodes.find("192.168.1.10:30002") == std::string::npos);
  ASSERT_TRUE(output_nodes.find("192.168.1.11:30004") == std::string::npos);
}

TEST_F(ClusterTest, ClusterSlotsWithLBAddress) {
  const std::string nodes_with_lb =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 192.168.1.10 30002 "
      "master - 0-5000 10.0.0.100 30002\n"
      "07c37dfeb235213a872192d90877d0cd55635b91 192.168.1.11 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 10.0.0.101 30004";
  
  auto config = storage_->GetConfig();
  config->workers = 0;
  Server server(storage_.get(), config);
  server.Stop();
  server.Join();
  
  Cluster cluster(&server, {"192.168.1.10"}, 30002);
  Status s = cluster.SetClusterNodes(nodes_with_lb, 1, false);
  ASSERT_TRUE(s.IsOK());
  
  std::vector<SlotInfo> slots_info;
  s = cluster.GetSlotsInfo(&slots_info);
  ASSERT_TRUE(s.IsOK());
  
  ASSERT_EQ(slots_info.size(), 1);
  ASSERT_EQ(slots_info[0].start, 0);
  ASSERT_EQ(slots_info[0].end, 5000);
  ASSERT_EQ(slots_info[0].nodes.size(), 2);
  
  // Verify master node uses LB address
  ASSERT_EQ(slots_info[0].nodes[0].host, "10.0.0.100");
  ASSERT_EQ(slots_info[0].nodes[0].port, 30002);
  
  // Verify replica node uses LB address
  ASSERT_EQ(slots_info[0].nodes[1].host, "10.0.0.101");
  ASSERT_EQ(slots_info[0].nodes[1].port, 30004);
}

TEST_F(ClusterTest, ClusterReplicasWithLBAddress) {
  const std::string nodes_with_lb =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 192.168.1.10 30002 "
      "master - 0-5000 10.0.0.100 30002\n"
      "07c37dfeb235213a872192d90877d0cd55635b91 192.168.1.11 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 10.0.0.101 30004\n"
      "07c37dfeb235213a872192d90877d0cd55635b92 192.168.1.12 30005 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 10.0.0.102 30005";
  
  auto config = storage_->GetConfig();
  config->workers = 0;
  Server server(storage_.get(), config);
  server.Stop();
  server.Join();
  
  Cluster cluster(&server, {"192.168.1.10"}, 30002);
  Status s = cluster.SetClusterNodes(nodes_with_lb, 1, false);
  ASSERT_TRUE(s.IsOK());
  
  auto replicas_result = cluster.GetReplicas("67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1");
  ASSERT_TRUE(replicas_result.IsOK());
  
  std::string replicas_output = replicas_result.GetValue();
  
  // Verify replicas output uses LB addresses
  ASSERT_TRUE(replicas_output.find("10.0.0.101:30004@40004") != std::string::npos);
  ASSERT_TRUE(replicas_output.find("10.0.0.102:30005@40005") != std::string::npos);
  
  // Verify real IPs are not in output
  ASSERT_TRUE(replicas_output.find("192.168.1.11:30004") == std::string::npos);
  ASSERT_TRUE(replicas_output.find("192.168.1.12:30005") == std::string::npos);
}

TEST_F(ClusterTest, ClusterNodesPersistenceWithLBAddress) {
  const std::string nodes_with_lb =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 192.168.1.10 30002 "
      "master - 0-5000 10.0.0.100 30002\n"
      "07c37dfeb235213a872192d90877d0cd55635b91 192.168.1.11 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 10.0.0.101 30004";
  
  auto config = storage_->GetConfig();
  config->workers = 0;
  Server server(storage_.get(), config);
  server.Stop();
  server.Join();
  
  Cluster cluster(&server, {"192.168.1.10"}, 30002);
  Status s = cluster.SetClusterNodes(nodes_with_lb, 1, false);
  ASSERT_TRUE(s.IsOK());
  
  // Test save to file
  std::string test_file = "/tmp/test_nodes_lb.conf";
  s = cluster.DumpClusterNodes(test_file);
  ASSERT_TRUE(s.IsOK());
  
  // Create new cluster instance to test loading
  Cluster cluster2(&server, {"192.168.1.10"}, 30002);
  s = cluster2.LoadClusterNodes(test_file);
  ASSERT_TRUE(s.IsOK());
  
  // Verify loaded node information
  std::string output_nodes;
  s = cluster2.GetClusterNodes(&output_nodes);
  ASSERT_TRUE(s.IsOK());
  
  // Verify LB addresses are correctly loaded and used
  ASSERT_TRUE(output_nodes.find("10.0.0.100:30002@40002") != std::string::npos);
  ASSERT_TRUE(output_nodes.find("10.0.0.101:30004@40004") != std::string::npos);
  
  // Clean up test file
  unlink(test_file.c_str());
}

TEST_F(ClusterTest, ClusterLBAddressEdgeCases) {
  auto config = storage_->GetConfig();
  config->workers = 0;
  Server server(storage_.get(), config);
  server.Stop();
  server.Join();
  
  Cluster cluster(&server, {"127.0.0.1"}, 3002);
  
  // Test LB address with "-" placeholder
  const std::string nodes_with_dash_lb =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 192.168.1.10 6379 "
      "master - 0-5000 - -";
  
  Status s = cluster.SetClusterNodes(nodes_with_dash_lb, 1, false);
  ASSERT_TRUE(s.IsOK());
  
  std::string output_nodes;
  s = cluster.GetClusterNodes(&output_nodes);
  ASSERT_TRUE(s.IsOK());
  
  // Verify uses real IP when LB address is "-"
  ASSERT_TRUE(output_nodes.find("192.168.1.10:6379@16379") != std::string::npos);
  
  // Test invalid LB port (should be ignored)
  const std::string nodes_invalid_lb =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa2 192.168.1.11 6379 "
      "master - 5001-10000 10.0.0.100 invalid_port";
  
  s = cluster.SetClusterNodes(nodes_invalid_lb, 2, false);
  ASSERT_TRUE(s.IsOK());  // Should succeed but ignore invalid LB address
}
