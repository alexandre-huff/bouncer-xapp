/*
# ==================================================================================
# Copyright (c) 2020 HCL Technologies Limited.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==================================================================================
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "msgs_proc.hpp"


bool XappMsgHandler::encode_subscription_delete_request(unsigned char* buffer, ssize_t *buf_len){

	subscription_helper sub_helper;
	sub_helper.set_request({0, 0, 0}); // requirement of subscription manager ... ?
	sub_helper.set_function_id(0);

	subscription_delete e2ap_sub_req_del;

	  // generate the delete request pdu

	  bool res = e2ap_sub_req_del.encode_e2ap_subscription(&buffer[0], buf_len, sub_helper);
	  if(! res){
	    mdclog_write(MDCLOG_ERR, "%s, %d: Error encoding subscription delete request pdu. Reason = %s", __FILE__, __LINE__, e2ap_sub_req_del.get_error().c_str());
	    return false;
	  }

	return true;

}

bool XappMsgHandler::decode_subscription_response(unsigned char* data_buf, size_t data_size){

	subscription_helper subhelper;
	subscription_response subresponse;
	bool res = true;
	E2AP_PDU_t *e2pdu = 0;

	asn_dec_rval_t rval;

	ASN_STRUCT_RESET(asn_DEF_E2AP_PDU, e2pdu);

	rval = asn_decode(0,ATS_ALIGNED_BASIC_PER, &asn_DEF_E2AP_PDU, (void**)&e2pdu, data_buf, data_size);
	switch(rval.code)
	{
		case RC_OK:
			   //Put in Subscription Response Object.
			   //asn_fprint(stdout, &asn_DEF_E2AP_PDU, e2pdu);
			   break;
		case RC_WMORE:
				mdclog_write(MDCLOG_ERR, "RC_WMORE");
				res = false;
				break;
		case RC_FAIL:
				mdclog_write(MDCLOG_ERR, "RC_FAIL");
				res = false;
				break;
		default:
				break;
	 }
	ASN_STRUCT_FREE(asn_DEF_E2AP_PDU, e2pdu);
	return res;

}

bool XappMsgHandler::a1_policy_handler(char * message, int *message_len, a1_policy_helper &helper){

  rapidjson::Document doc;
  if (doc.Parse<kParseStopWhenDoneFlag>(message).HasParseError()){
    mdclog_write(MDCLOG_ERR, "Error: %s, %d :: Could not decode A1 JSON message %s\n", __FILE__, __LINE__, message);
    return false;
  }

  //Extract Operation
  rapidjson::Pointer temp1("/operation");
  rapidjson::Value * ref1 = temp1.Get(doc);
  if (ref1 == NULL){
    mdclog_write(MDCLOG_ERR, "Error : %s, %d:: Could not extract policy type id from %s\n", __FILE__, __LINE__, message);
    return false;
  }
  helper.operation = ref1->GetString();

  // Extract policy id type
  rapidjson::Pointer temp2("/policy_type_id");
  rapidjson::Value * ref2 = temp2.Get(doc);
  if (ref2 == NULL){
    mdclog_write(MDCLOG_ERR, "Error : %s, %d:: Could not extract policy type id from %s\n", __FILE__, __LINE__, message);
    return false;
  }
  //helper.policy_type_id = ref2->GetString();
  helper.policy_type_id = to_string(ref2->GetInt());

  // Extract policy instance id
  rapidjson::Pointer temp("/policy_instance_id");
  rapidjson::Value * ref = temp.Get(doc);
  if (ref == NULL){
    mdclog_write(MDCLOG_ERR, "Error : %s, %d:: Could not extract policy type id from %s\n", __FILE__, __LINE__, message);
    return false;
  }
  helper.policy_instance_id = ref->GetString();

  // ########### Extract payload ###########
  rapidjson::Pointer ptemp("/payload");
  rapidjson::Value * pref = ptemp.Get(doc);

  // Validating json message based on the schema file
  rapidjson::SchemaValidator validator(*policy_schema.get());
  if (!pref->Accept(validator)) {
	  // Input JSON is invalid according to the schema
    // Output diagnostic information
    StringBuffer sb;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    mdclog_write(MDCLOG_ERR, "Error: %s, %d :: Invalid schema: %s\n", __FILE__, __LINE__, sb.GetString());
    mdclog_write(MDCLOG_ERR, "Error: %s, %d :: Invalid keyword: %s\n", __FILE__, __LINE__, validator.GetInvalidSchemaKeyword());
    sb.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
    mdclog_write(MDCLOG_ERR, "Error: %s, %d :: Invalid document: %s\n", __FILE__, __LINE__, sb.GetString());

    return false;
  }

  // Extract list of ues
  rapidjson::Pointer temp_ue("/payload/ue_rc");
  rapidjson::Value * ue_rc = temp_ue.Get(doc);
  if (ue_rc == NULL){
    mdclog_write(MDCLOG_ERR, "Error : %s, %d:: Could not extract ue_rc from %s\n", __FILE__, __LINE__, message);
    return false;
  }

  for (auto& v : ue_rc->GetArray()) {
    rapidjson::Pointer ue_index("/ue_index");
    rapidjson::Value *ue_value = ue_index.Get(v);
    rapidjson::Pointer max_prb("/max_prb");
    rapidjson::Value *prb_value = max_prb.Get(v);

    shared_ptr<ue_rc_helper> ue_helper = make_shared<ue_rc_helper>();
    ue_helper->ue_index = ue_value->GetInt();
    ue_helper->max_prb = prb_value->GetInt();

    helper.ue_list.emplace_back(ue_helper);
  }
  // ########### Extract payload up to here ###########

  // Preparing response message to A1 Mediator
  if (helper.policy_type_id == "20008" && helper.operation == "CREATE"){  // FIXME policy type id hardcoded here
    helper.status = "OK";
    Document::AllocatorType& alloc = doc.GetAllocator();

    Value handler_id;
    handler_id.SetString(helper.handler_id.c_str(), helper.handler_id.length(), alloc);

    Value status;
    status.SetString(helper.status.c_str(), helper.status.length(), alloc);

    doc.AddMember("handler_id", handler_id, alloc);
    doc.AddMember("status",status, alloc);
    doc.RemoveMember("operation");
    doc.RemoveMember("payload");
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);
    strncpy(message,buffer.GetString(), buffer.GetLength());
    *message_len = buffer.GetLength();

    return true;
  }

  return false;
}

//For processing received messages.XappMsgHandler should mention if resend is required or not.
void XappMsgHandler::operator()(rmr_mbuf_t *message, bool *resend)
{

	if (message->len > MAX_RMR_RECV_SIZE)
	{
		mdclog_write(MDCLOG_ERR, "Error : %s, %d, RMR message larger than %d. Ignoring ...", __FILE__, __LINE__, MAX_RMR_RECV_SIZE);
		return;
	}

	E2AP_PDU_t* e2pdu = (E2AP_PDU_t*)calloc(1, sizeof(E2AP_PDU));
	// int num = 0;

	switch (message->mtype)
	{
		// need to fix the health check.
		case (RIC_HEALTH_CHECK_REQ):
			message->mtype = RIC_HEALTH_CHECK_RESP; // if we're here we are running and all is ok
			message->sub_id = -1;
			strncpy((char *)message->payload, "Bouncer OK\n", rmr_payload_size(message));
			*resend = true;
			break;

		case (RIC_SUB_RESP):
			mdclog_write(MDCLOG_INFO, "Received subscription message of type = %d", message->mtype);
			unsigned char *me_id;
			if ((me_id = (unsigned char *)malloc(sizeof(unsigned char) * RMR_MAX_MEID)) == NULL)
			{
				mdclog_write(MDCLOG_ERR, "Error :  %s, %d : malloc failed for me_id", __FILE__, __LINE__);
				me_id = rmr_get_meid(message, NULL);
			}
			else
			{
				rmr_get_meid(message, me_id);
			}
			if (me_id == NULL)
			{
				mdclog_write(MDCLOG_ERR, " Error :: %s, %d : rmr_get_meid failed me_id is NULL", __FILE__, __LINE__);
				break;
			}
			mdclog_write(MDCLOG_INFO, "RMR Received MEID: %s", me_id);
			if (_ref_sub_handler != NULL)
			{
				_ref_sub_handler->manage_subscription_response(message->mtype, reinterpret_cast<char const *>(me_id));
			}
			else
			{
				mdclog_write(MDCLOG_ERR, " Error :: %s, %d : Subscription handler not assigned in message processor !", __FILE__, __LINE__);
			}
			*resend = false;
			if (me_id != NULL)
			{
				mdclog_write(MDCLOG_INFO, "Free RMR Received MEID memory: %s(0x%p)", me_id, me_id);
				free(me_id);
			}
			break;

		case (RIC_SUB_DEL_RESP):
			mdclog_write(MDCLOG_INFO, "Received subscription delete message of type = %d", message->mtype);
			// unsigned char *me_id;
			if ((me_id = (unsigned char *)malloc(sizeof(unsigned char) * RMR_MAX_MEID)) == NULL)
			{
				mdclog_write(MDCLOG_ERR, "Error :  %s, %d : malloc failed for me_id", __FILE__, __LINE__);
				me_id = rmr_get_meid(message, NULL);
			}
			else

			{
				rmr_get_meid(message, me_id);
			}
			if (me_id == NULL)
			{
				mdclog_write(MDCLOG_ERR, " Error :: %s, %d : rmr_get_meid failed me_id is NULL", __FILE__, __LINE__);
				break;
			}
			mdclog_write(MDCLOG_INFO, "RMR Received MEID: %s", me_id);
			if (_ref_sub_handler != NULL)
			{
				_ref_sub_handler->manage_subscription_response(message->mtype, reinterpret_cast<char const *>(me_id));
			}
			else
			{
				mdclog_write(MDCLOG_ERR, " Error :: %s, %d : Subscription handler not assigned in message processor !", __FILE__, __LINE__);
			}
			*resend = false;
			if (me_id != NULL)
			{
				mdclog_write(MDCLOG_INFO, "Free RMR Received MEID memory: %s(0x%p)", me_id, me_id);
				free(me_id);
			}
			break;

		case (RIC_INDICATION):
		{
			mdclog_write(MDCLOG_DEBUG, "Decoding indication for msg = %d", message->mtype);

			ASN_STRUCT_RESET(asn_DEF_E2AP_PDU, e2pdu);
			asn_transfer_syntax syntax;
			syntax = ATS_ALIGNED_BASIC_PER;

			mdclog_write(MDCLOG_DEBUG, "Data_size = %d", message->len);

			auto rval = asn_decode(nullptr, syntax, &asn_DEF_E2AP_PDU, (void **)&e2pdu, message->payload, message->len);

			if (rval.code == RC_OK)
			{
				mdclog_write(MDCLOG_DEBUG, "rval.code = %d ", rval.code);
			}
			else
			{
				mdclog_write(MDCLOG_ERR, " rval.code = %d ", rval.code);
				break;
			}

			if (mdclog_level_get() > MDCLOG_INFO)
				asn_fprint(stderr, &asn_DEF_E2AP_PDU, e2pdu);

			ric_indication indication;
			ric_indication_helper ind_helper;
			string error_msg;
			indication.get_fields(e2pdu->choice.initiatingMessage, ind_helper);

			// TODO ######## this is specific for E2SM-RC
			// uint8_t ctrl_header_buf[8192] = {0, };
			// ssize_t ctrl_header_buf_size = 8192;

			// UEID_t *ueid = ind_helper.get_ui_id();

			// e2sm_control e2sm_control;
			// bool ret_head = e2sm_control.encode_rc_control_header(ctrl_header_buf, &ctrl_header_buf_size, ueid);
			// ASN_STRUCT_FREE(asn_DEF_UEID, ueid);	// we have to release here to avoid memory leaks if encoding returns false
			// if (!ret_head) {
			// 	mdclog_write(MDCLOG_ERR, "%s", e2sm_control.get_error().c_str());
			// 	*resend = false;
			// 	break;
			// }

			// uint8_t ctrl_msg_buf[8192] = {0, };
			// ssize_t ctrl_msg_buf_size = 8192;

			// bool ret_msg = e2sm_control.encode_rc_control_message(ctrl_msg_buf, &ctrl_msg_buf_size);
			// if (!ret_msg) {
			// 	mdclog_write(MDCLOG_ERR, "%s", e2sm_control.get_error().c_str());
			// 	*resend = false;
			// 	break;
			// }

			// // E2AP Control Helper
			// ric_control_helper helper;
			// helper.requestor_id = ind_helper.request_id.ricRequestorID;
			// helper.instance_id = ind_helper.request_id.ricInstanceID;
			// helper.func_id = ind_helper.func_id;
			// // Control Call Process ID
			// helper.call_process_id = ind_helper.call_process_id.buf;
			// helper.call_process_id_size = ind_helper.call_process_id.size;
			// // Control ACK
			// helper.control_ack = RICcontrolAckRequest_noAck; // for now we do not require ACK messages for control requests
			// // Control Header
			// helper.control_header = ctrl_header_buf;
			// helper.control_header_size = ctrl_header_buf_size;
			// // Control Message
			// helper.control_msg = ctrl_msg_buf;
			// helper.control_msg_size = ctrl_msg_buf_size;

			// // E2AP buffer
			// uint8_t e2ap_buf[8192] = {0, };
			// ssize_t e2ap_buf_size = 8192;

			// ric_control_request control_req;
			// bool encoded = control_req.encode_e2ap_control_request(e2ap_buf, &e2ap_buf_size, helper);
			// if (encoded) {
			// 	message->mtype = RIC_CONTROL_REQ; // if we're here we are running and all is ok
			// 	message->sub_id = -1;

			// 	int rmr_len = rmr_payload_size(message);
			// 	if (rmr_len < 0) {
			// 		mdclog_write(MDCLOG_ERR, "unable to get the rmr payload size for control request. Reason = %s", strerror(errno));
			// 		*resend = false;
			// 		break;
			// 	}

			// 	if (e2ap_buf_size <= (ssize_t)rmr_len) {	// avoid compiler comparison complains
			// 		memcpy(message->payload, e2ap_buf, e2ap_buf_size);
			// 		message->len = e2ap_buf_size;
			// 		*resend = true;

			// 	} else {
			// 		mdclog_write(MDCLOG_ERR, "E2AP Control Request encoded size %lu exceeds rmr payload size %d", e2ap_buf_size, rmr_len);
			// 		*resend = false;
			// 	}
			// } else {
			// 	mdclog_write(MDCLOG_ERR, "E2AP Control Request encoding error. Reason = %s", control_req.get_error().c_str());
			// 	*resend = false;
			// }
			// TODO ######## end of specific for E2SM-RC


			if (mdclog_level_get() > MDCLOG_INFO)
				fprintf(stderr, "end of RIC_INDICATION case\n\n");
			// num++;
			// mdclog_write(MDCLOG_INFO, "Number of Indications Received = %d", num);
			break;
		}

		case A1_POLICY_REQ:
		{
			mdclog_write(MDCLOG_INFO, "In Message Handler: Received A1_POLICY_REQ.");

      a1_policy_helper helper;
      helper.handler_id = xapp_id;

			bool res=false;

			res = a1_policy_handler((char*)message->payload, &message->len, helper);
			if(res)
			{
				message->mtype = A1_POLICY_RESP;        // if we're here we are running and all is ok
				message->sub_id = -1;
				*resend = true;
			}
			break;

		}
		default:
			mdclog_write(MDCLOG_ERR, "Error :: Unknown message type %d received from RMR", message->mtype);
			*resend = false;
	}

	ASN_STRUCT_FREE(asn_DEF_E2AP_PDU, e2pdu);
}


