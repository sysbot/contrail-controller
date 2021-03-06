/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __testflow__
#define __testflow__
#include "test_pkt_util.h"
class TestFlowPkt {
public:
    //Ingress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, uint32_t ifindex) : sip_(sip), 
                dip_(dip), proto_(proto), sport_(sport), dport_(dport), 
                ifindex_(ifindex), mpls_(0), hash_(0) {
        vrf_ = 
          Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf)->GetVrfId();
    };

    //Ingress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, uint32_t ifindex, uint32_t hash):
                sip_(sip), dip_(dip), proto_(proto), sport_(sport), dport_(dport),
                ifindex_(ifindex), mpls_(0), hash_(hash) {
         vrf_ = 
          Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf)->GetVrfId();
     };


    //Egress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, std::string osip, uint32_t mpls) :
                sip_(sip), dip_(dip), proto_(proto), sport_(sport), dport_(dport),
                ifindex_(0), mpls_(mpls), outer_sip_(osip), hash_(0) {
        vrf_ = 
         Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf)->GetVrfId();
    };

    //Egress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, std::string osip, uint32_t mpls,
                uint32_t hash) : sip_(sip), dip_(dip), proto_(proto), sport_(sport),
                dport_(dport), ifindex_(0), mpls_(mpls), hash_(hash) {
         vrf_ = 
          Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf)->GetVrfId();
     };

    void SendIngressFlow() {
        if (!hash_) {
            hash_ = rand() % 1000;
        }

        switch(proto_) {
        case IPPROTO_TCP:
            TxTcpPacket(ifindex_, sip_.c_str(), dip_.c_str(), sport_, dport_, 
                        hash_, vrf_);
            break;

        case IPPROTO_UDP:
            TxUdpPacket(ifindex_, sip_.c_str(), dip_.c_str(), sport_, dport_, 
                        hash_, vrf_);
            break;

        default:
            TxIpPacket(ifindex_, sip_.c_str(), dip_.c_str(), proto_, hash_);
            break;
        }
    };

    void SendEgressFlow() {
        if (!hash_) {
            hash_ = rand() % 65535;
        }

        std::string self_server = Agent::GetInstance()->GetRouterId().to_string();
        //Populate ethernet interface id
        uint32_t eth_intf_id = 
            EthInterfaceGet(Agent::GetInstance()->
                    GetIpFabricItfName().c_str())->GetInterfaceId();
        switch(proto_) {
        case IPPROTO_TCP:
            TxTcpMplsPacket(eth_intf_id, outer_sip_.c_str(), 
                            self_server.c_str(), mpls_, sip_.c_str(),
                            dip_.c_str(), sport_, dport_, hash_);
            break;

        case IPPROTO_UDP:
            TxUdpMplsPacket(eth_intf_id, outer_sip_.c_str(), 
                            self_server.c_str(), mpls_, sip_.c_str(), 
                            dip_.c_str(), sport_, dport_, hash_);
            break;

        default:
            TxIpMplsPacket(eth_intf_id, outer_sip_.c_str(), 
                           self_server.c_str(), mpls_, sip_.c_str(), 
                           dip_.c_str(), proto_, hash_);
            break;
        }
    };

    FlowEntry* Send() {
        if (ifindex_) {
            SendIngressFlow();
        } else if (mpls_) {
            SendEgressFlow();
        } else {
            assert(0);
        }
        client->WaitForIdle();
        //Get flow 
        FlowEntry *fe = FlowGet(vrf_, sip_, dip_, proto_, sport_, dport_);
        EXPECT_TRUE(fe != NULL);
        return fe;
    };

    void Delete() {
        FlowKey key;
        key.vrf = vrf_;
        key.src.ipv4 = ntohl(inet_addr(sip_.c_str()));
        key.dst.ipv4 = ntohl(inet_addr(dip_.c_str()));
        key.src_port = sport_;
        key.dst_port = dport_;
        key.protocol = proto_;

        if (FlowTable::GetFlowTableObject()->Find(key) == NULL) {
            return;
        }

        FlowTable::GetFlowTableObject()->DeleteNatFlow(key, true);
        client->WaitForIdle();
        EXPECT_TRUE(FlowTable::GetFlowTableObject()->Find(key) == NULL);
    };

private:
    std::string    sip_;
    std::string    dip_;
    uint16_t       proto_;
    uint32_t       sport_;
    uint32_t       dport_;
    uint32_t       vrf_;
    uint32_t       ifindex_;
    uint32_t       mpls_;
    std::string    outer_sip_;
    uint32_t       hash_; 
};

class FlowVerify {
public:
    virtual ~FlowVerify() {};
    virtual void Verify(FlowEntry *fe) = 0;
};

class VerifyVn : public FlowVerify {
public:
    VerifyVn(std::string src_vn, std::string dest_vn):
        src_vn_(src_vn), dest_vn_(dest_vn) {};
    virtual ~VerifyVn() {};

    virtual void Verify(FlowEntry *fe) {
        EXPECT_TRUE(fe->data.source_vn == src_vn_);
        EXPECT_TRUE(fe->data.dest_vn == dest_vn_);

        if (true) {
            FlowEntry *rev = fe->data.reverse_flow.get();
            EXPECT_TRUE(rev != NULL);
            EXPECT_TRUE(rev->data.source_vn == dest_vn_);
            EXPECT_TRUE(rev->data.dest_vn == src_vn_);
        }
    };

private:
    std::string src_vn_;
    std::string dest_vn_;
};

class VerifyVrf : public FlowVerify {
public:
    VerifyVrf(std::string src_vrf, std::string dest_vrf):
        src_vrf_(src_vrf), dest_vrf_(dest_vrf) {};
    virtual ~VerifyVrf() {};

    void Verify(FlowEntry *fe) {
        const VrfEntry *src_vrf = 
            Agent::GetInstance()->GetVrfTable()->FindVrfFromName(src_vrf_);
        EXPECT_TRUE(src_vrf != NULL);

        const VrfEntry *dest_vrf = 
            Agent::GetInstance()->GetVrfTable()->FindVrfFromName(dest_vrf_);
        EXPECT_TRUE(dest_vrf != NULL);

        EXPECT_TRUE(fe->data.flow_source_vrf == src_vrf->GetVrfId());
        EXPECT_TRUE(fe->data.flow_dest_vrf == dest_vrf->GetVrfId());

        if (true) {
            FlowEntry *rev = fe->data.reverse_flow.get();
            EXPECT_TRUE(rev != NULL);
            EXPECT_TRUE(rev->data.flow_source_vrf == dest_vrf->GetVrfId());
            EXPECT_TRUE(rev->data.flow_dest_vrf == src_vrf->GetVrfId());
        }
    };

private:
    std::string src_vrf_;
    std::string dest_vrf_;
};

struct VerifyNat : public FlowVerify {
public:
    VerifyNat(std::string nat_sip, std::string nat_dip, uint32_t proto,
              uint32_t nat_sport, uint32_t nat_dport):
              nat_sip_(nat_sip), nat_dip_(nat_dip), nat_sport_(nat_sport),
              nat_dport_(nat_dport) { };
    virtual ~VerifyNat() { };

    void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->data.reverse_flow.get();
        EXPECT_TRUE(rev != NULL);
        EXPECT_TRUE(rev->key.src.ipv4 == ntohl(inet_addr(nat_sip_.c_str())));
        EXPECT_TRUE(rev->key.dst.ipv4 == ntohl(inet_addr(nat_dip_.c_str())));
        EXPECT_TRUE(rev->key.src_port == nat_sport_);
        EXPECT_TRUE(rev->key.dst_port == nat_dport_);
    };

private:
    std::string nat_sip_;
    std::string nat_dip_;
    uint32_t    nat_sport_;
    uint32_t    nat_dport_;
};

class VerifyEcmp : public FlowVerify {
public:
    VerifyEcmp(bool fwd_ecmp, bool rev_ecmp):
        fwd_flow_is_ecmp_(fwd_ecmp), rev_flow_is_ecmp_(rev_ecmp) { };
    virtual ~VerifyEcmp() { };
    void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->data.reverse_flow.get();
        EXPECT_TRUE(rev != NULL);

        if (fwd_flow_is_ecmp_) {
            EXPECT_TRUE(fe->data.ecmp == true);
            EXPECT_TRUE(fe->data.component_nh_idx != -1);
        } else {
            EXPECT_TRUE(fe->data.ecmp == false);
            EXPECT_TRUE(fe->data.component_nh_idx == -1);
        }

        if (rev_flow_is_ecmp_) {
            EXPECT_TRUE(rev->data.ecmp == true);
            EXPECT_TRUE(rev->data.component_nh_idx != -1);
        } else {
            EXPECT_TRUE(rev->data.ecmp == false);
            EXPECT_TRUE(rev->data.component_nh_idx == -1);
        }
    };

private:
    bool fwd_flow_is_ecmp_;
    bool rev_flow_is_ecmp_;
};

struct TestFlow {
    ~TestFlow() {
        int i = 0;
        for (int i = 0; i < 10; i++) {
            if (action_[i]) {
                delete action_[i];
            }
        }
    };

    void Verify(FlowEntry *fe) {
        for (int i = 0; i < 10; i++) {
            if (action_[i]) {
                action_[i]->Verify(fe);
            }
        }
    };

    FlowEntry* Send() {
        return pkt_.Send();
    };

    void Delete() {
        pkt_.Delete();
    };

    TestFlowPkt pkt_;
    FlowVerify* action_[10];
};

void CreateFlow(TestFlow *tflow, uint32_t count) {
    for (int i = 0; i < count; i++) {
        FlowEntry *fe = tflow->Send();
        tflow->Verify(fe);
        tflow = tflow + 1;
    }
}

void DeleteFlow(TestFlow *tflow, uint32_t count) {
    for (int i = 0; i < count; i++) {
        tflow->Delete();
    }
}
#endif
