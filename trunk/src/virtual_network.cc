/*
* ipop-project
* Copyright 2016, University of Florida
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/
#include "virtual_network.h"
#include "webrtc/base/base64.h"
#include "tincan_control.h"
namespace tincan
{
VirtualNetwork::VirtualNetwork(
  unique_ptr<OverlayDescriptor> descriptor,
  IpopControllerLink * ctrl_handle) :
  Overlay(move(descriptor), ctrl_handle)
{
  peer_network_ = make_unique<PeerNetwork>();
}

VirtualNetwork::~VirtualNetwork()
{}

shared_ptr<VirtualLink>
VirtualNetwork::CreateVlink(
  unique_ptr<VlinkDescriptor> vlink_desc,
  unique_ptr<PeerDescriptor> peer_desc)
{
  shared_ptr<VirtualLink> vl;
  //MacAddressType mac;
  //string mac_str = peer_desc->mac_address;
  //StringToByteArray(peer_desc->mac_address, mac.begin(), mac.end());
  if(peer_network_->Exists(vlink_desc->uid))
  {
    vl = peer_network_->GetVlinkById(vlink_desc->uid);
    vl->PeerCandidates(peer_desc->cas);
    vl->StartConnections();
    LOG(LS_INFO) << "Added remote CAS to vlink w/ peer " << peer_desc->uid;
  }
  else
  {
    cricket::IceRole ir = cricket::ICEROLE_CONTROLLED;
    if(local_fingerprint_->ToString() < peer_desc->fingerprint)
      ir = cricket::ICEROLE_CONTROLLING;
    string roles[] = { "CONTROLLED", "CONTROLLING" };
    LOG(LS_INFO) << "Creating " << roles[ir] << " vlink w/ peer "
      << peer_desc->uid;
    vl = Overlay::CreateVlink(move(vlink_desc), move(peer_desc), ir);
    peer_network_->Add(vl);
  }
  return vl;
}

void
VirtualNetwork::Start()
{
  Overlay::Start();
  tdev_->Up();
  peer_net_thread_.Start(peer_network_.get());
}

void
VirtualNetwork::Shutdown()
{
  Overlay::Shutdown();
  peer_net_thread_.Quit();
  peer_network_->Clear();
}

void
VirtualNetwork::UpdateRouteTable(
  const Json::Value & rt_descr)
{
  //peer_network_->UpdateRouteTable(rt_descr);
}


void VirtualNetwork::RemoveLink(
  const string & vlink_id)
{
    peer_network_->Remove(vlink_id);
}

void VirtualNetwork::QueryInfo(
  Json::Value & olay_info)
{
  olay_info[TincanControl::OverlayId] = descriptor_->uid;
  olay_info[TincanControl::FPR] = Fingerprint();
  olay_info[TincanControl::TapName] = tap_desc_->name;
  olay_info[TincanControl::MAC] = MacAddress();
  olay_info[TincanControl::VIP4] = tap_desc_->ip4;
  olay_info[TincanControl::IP4PrefixLen] = tap_desc_->prefix4;
  olay_info[TincanControl::MTU4] = tap_desc_->mtu4;
  //Add the uid for each vlink
  olay_info[TincanControl::Vlinks] = Json::Value(Json::arrayValue);
  vector<string> vlids = peer_network_->QueryVlinks();
  for(auto vl: vlids)
  {
    olay_info[TincanControl::Vlinks].append(vl);
  }
}

void VirtualNetwork::QueryLinkCas(
  const string & vlink_id,
  Json::Value & cas_info)
{
  if(peer_network_->Exists(vlink_id))
  {
    shared_ptr<VirtualLink> vl = peer_network_->GetVlink(vlink_id);
    if(vl->IceRole() == cricket::ICEROLE_CONTROLLING)
      cas_info[TincanControl::IceRole] = TincanControl::Controlling.c_str();
    else if(vl->IceRole() == cricket::ICEROLE_CONTROLLED)
      cas_info[TincanControl::IceRole] = TincanControl::Controlled.c_str();

    cas_info[TincanControl::CAS] = vl->Candidates();
  }
}

void
VirtualNetwork::QueryLinkIds
(vector<string>& link_ids)
{
  link_ids = peer_network_->QueryVlinks();
}

void
VirtualNetwork::QueryLinkInfo(
  const string & vlink_id,
  Json::Value & vlink_info)
{
  if(peer_network_->Exists(vlink_id))
  {
    shared_ptr<VirtualLink> vl = peer_network_->GetVlinkById(vlink_id);
    if(vl->IceRole() == cricket::ICEROLE_CONTROLLING)
      vlink_info[TincanControl::IceRole] = TincanControl::Controlling;
    else
      vlink_info[TincanControl::IceRole] = TincanControl::Controlled;
    if(vl && vl->IsReady())
    {
      LinkInfoMsgData md;
      md.vl = vl;
      net_worker_.Post(RTC_FROM_HERE, this, MSGID_QUERY_NODE_INFO, &md);
      md.msg_event.Wait(Event::kForever);
      vlink_info[TincanControl::Stats].swap(md.info);
      vlink_info[TincanControl::Status] = "ONLINE";
    }
    else
    {
      vlink_info[TincanControl::Status] = "OFFLINE";
      vlink_info[TincanControl::Stats] = Json::Value(Json::objectValue);
    }
  }
  else
  {
    vlink_info[TincanControl::Status] = "UNKNOWN";
    vlink_info[TincanControl::Stats] = Json::Value(Json::objectValue);
  }
}

void
VirtualNetwork::SendIcc(
  const string & vlink_id,
  const string & data)
{
  if(!peer_network_->Exists(vlink_id))
    throw TCEXCEPT("No vlink exists by the specified id");

  unique_ptr<IccMessage> icc = make_unique<IccMessage>();
  icc->Message((uint8_t*)data.c_str(), (uint16_t)data.length());
  unique_ptr<TransmitMsgData> md = make_unique<TransmitMsgData>();
  md->frm = move(icc);
  md->vl = peer_network_->GetVlinkById(vlink_id);
  net_worker_.Post(RTC_FROM_HERE, this, MSGID_SEND_ICC, md.release());
}

/*
Incoming frames off the vlink are one of:
pure ethernet frame - to be delivered to the TAP device
icc message - to be delivered to the local controller
The implementing entity needs access to the TAP and controller instances to
transmit the frame. These can be provided at initialization.
Responsibility: Identify the received frame type, perfrom a transformation of
the frame
if needed and transmit it.
 - Is this my ARP? Deliver to TAP.
 - Is this an ICC? Send to controller.
Types of Transformation:

*/
void
VirtualNetwork::VlinkReadComplete(
  uint8_t * data,
  uint32_t data_len,
  VirtualLink & vlink)
{
  unique_ptr<TapFrame> frame = make_unique<TapFrame>(data, data_len);
  TapFrameProperties fp(*frame);
  if(fp.IsIccMsg())
  { // this is an ICC message, deliver to the ipop-controller
    unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
    ctrl->SetControlType(TincanControl::CTTincanRequest);
    Json::Value & req = ctrl->GetRequest();
    req[TincanControl::Command] = TincanControl::ICC;
    req[TincanControl::OverlayId] = descriptor_->uid;
    req[TincanControl::LinkId] = vlink.Id();
    req[TincanControl::Data] = string((char*)frame->Payload(),
      frame->PayloadLength());
    ctrl_link_->Deliver(move(ctrl));
  }
  else if(fp.IsFwdMsg())
  { // a frame to be routed on the overlay
    if(peer_network_->IsRouteExists(fp.DestinationMac()))
    {
      shared_ptr<VirtualLink> vl = peer_network_->GetRoute(fp.DestinationMac());
      TransmitMsgData *md = new TransmitMsgData;
      md->frm = move(frame);
      md->vl = vl;
      net_worker_.Post(RTC_FROM_HERE, this, MSGID_FWD_FRAME, md);
    }
    else
    { //no route found, send to controller
      unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
      ctrl->SetControlType(TincanControl::CTTincanRequest);
      Json::Value & req = ctrl->GetRequest();
      req[TincanControl::Command] = TincanControl::ReqRouteUpdate;
      req[TincanControl::OverlayId] = descriptor_->uid;
      req[TincanControl::Data] = ByteArrayToString(frame->Payload(),
        frame->PayloadEnd());
      ctrl_link_->Deliver(move(ctrl));
    }
  }
  else if (fp.IsDtfMsg())
  {
    frame->Dump("Frame from vlink");
    frame->BufferToTransfer(frame->Payload()); //write frame payload to TAP
    frame->BytesToTransfer(frame->PayloadLength());
    frame->SetWriteOp();
    tdev_->Write(*frame.release());
  }
  else
  {
    LOG(LS_ERROR) << "Unknown frame type received!";
    frame->Dump("Invalid header");
  }
}

//
//AsyncIOCompletion Routines for TAP device
/*
Frames read from the TAP device are handled here. This is an ethernet frame
from the networking stack. The implementing entity needs access to the
recipient  - via its vlink, or to the controller - when there is no
vlink to the recipient.
Responsibility: Identify the recipient of the frame and route accordingly.
- Is this an ARP? Send to controller.
- Is this an IP packet? Use MAC to lookup vlink and forwrd or send to
controller.
- Is this for a device behind an IPOP switch

Note: Avoid exceptions on the IO loop
*/
void
VirtualNetwork::TapReadComplete(
  AsyncIo * aio_rd)
{
  TapFrame * frame = static_cast<TapFrame*>(aio_rd->context_);
  if(!aio_rd->good_)
  {
    frame->Initialize();
    frame->BufferToTransfer(frame->Payload());
    frame->BytesToTransfer(frame->PayloadCapacity());
    if(0 != tdev_->Read(*frame))
      delete frame;
    return;
  }
  frame->PayloadLength(frame->BytesTransferred());
  TapFrameProperties fp(*frame);
  MacAddressType mac = fp.DestinationMac();
  frame->BufferToTransfer(frame->Begin()); //write frame header + PL to vlink
  frame->BytesToTransfer(frame->Length());
  if(peer_network_->IsAdjacent(mac))
  {
    frame->Header(kDtfMagic);
    //frame->Dump("Unicast");
    shared_ptr<VirtualLink> vl = peer_network_->GetVlink(mac);
    TransmitMsgData *md = new TransmitMsgData;
    md->frm.reset(frame);
    md->vl = vl;
    net_worker_.Post(RTC_FROM_HERE, this, MSGID_TRANSMIT, md);
  }
  else if(peer_network_->IsRouteExists(mac))
  {
    frame->Header(kFwdMagic);
    //frame->Dump("Frame FWD");
    TransmitMsgData *md = new TransmitMsgData;
    md->frm.reset(frame);
    md->vl = peer_network_->GetRoute(mac);
    net_worker_.Post(RTC_FROM_HERE, this, MSGID_FWD_FRAME, md);
  }
  else
  {
    frame->Header(kIccMagic);
    //Send to IPOP Controller to find a route for this frame
    unique_ptr<TincanControl> ctrl = make_unique<TincanControl>();
    ctrl->SetControlType(TincanControl::CTTincanRequest);
    Json::Value & req = ctrl->GetRequest();
    req[TincanControl::Command] = TincanControl::ReqRouteUpdate;
    req[TincanControl::OverlayId] = descriptor_->uid;
    req[TincanControl::Data] = ByteArrayToString(frame->Payload(),
      frame->PayloadEnd());
    ctrl_link_->Deliver(move(ctrl));
    //Post a new TAP read request
    frame->Initialize(frame->Payload(), frame->PayloadCapacity());
    if(0 != tdev_->Read(*frame))
      delete frame;
  }
}

void
VirtualNetwork::TapWriteComplete(
  AsyncIo * aio_wr)
{
  TapFrame * frame = static_cast<TapFrame*>(aio_wr->context_);
  if(frame->IsGood())
    frame->Dump("TAP Write Completed");
  else
    LOG(LS_WARNING) << "Tap Write FAILED completion";
  delete frame;
}

} //namespace tincan
