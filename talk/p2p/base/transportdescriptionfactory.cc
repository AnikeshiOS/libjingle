/*
 * libjingle
 * Copyright 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "talk/p2p/base/transportdescriptionfactory.h"

#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/messagedigest.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/sslfingerprint.h"
#include "talk/p2p/base/transportdescription.h"

namespace cricket {

static TransportProtocol kDefaultProtocol = ICEPROTO_GOOGLE;
static const char* kDefaultDigestAlg = talk_base::DIGEST_SHA_256;

TransportDescriptionFactory::TransportDescriptionFactory()
    : protocol_(kDefaultProtocol),
      secure_(SEC_DISABLED),
      identity_(NULL),
      digest_alg_(kDefaultDigestAlg) {
}

TransportDescription* TransportDescriptionFactory::CreateOffer(
    const TransportDescription* current_description) const {
  talk_base::scoped_ptr<TransportDescription> desc(new TransportDescription());

  // Set the transport type depending on the selected protocol.
  if (protocol_ == ICEPROTO_RFC5245) {
    desc->transport_type = NS_JINGLE_ICE_UDP;
  } else if (protocol_ == ICEPROTO_HYBRID) {
    desc->transport_type = NS_JINGLE_ICE_UDP;
    desc->AddOption(ICE_OPTION_GICE);
  } else if (protocol_ == ICEPROTO_GOOGLE) {
    desc->transport_type = NS_GINGLE_P2P;
  }

  // Generate the ICE credentials if we don't already have them.
  if (!current_description) {
    desc->ice_ufrag = talk_base::CreateRandomString(ICE_UFRAG_LENGTH);
    desc->ice_pwd = talk_base::CreateRandomString(ICE_PWD_LENGTH);
  } else {
    desc->ice_ufrag = current_description->ice_ufrag;
    desc->ice_pwd = current_description->ice_pwd;
  }

  // If we are trying to establish a secure transport, add a fingerprint.
  if (secure_ == SEC_ENABLED || secure_ == SEC_REQUIRED) {
    // Fail if we can't create the fingerprint.
    if (!CreateIdentityDigest(desc.get())) {
      return NULL;
    }
  }
  return desc.release();
}

TransportDescription* TransportDescriptionFactory::CreateAnswer(
    const TransportDescription* offer,
    const TransportDescription* current_description) const {
  // A NULL offer is treated as a GICE transport description.
  // TODO(juberti): Figure out why we get NULL offers, and fix this upstream.
  talk_base::scoped_ptr<TransportDescription> desc(new TransportDescription());

  // Figure out which ICE variant to negotiate; prefer RFC 5245 ICE, but fall
  // back to G-ICE if needed. Note that we never create a hybrid answer, since
  // we know what the other side can support already.
  if (offer && offer->transport_type == NS_JINGLE_ICE_UDP &&
      (protocol_ == ICEPROTO_RFC5245 || protocol_ == ICEPROTO_HYBRID)) {
    // Offer is ICE or hybrid, we support ICE or hybrid: use ICE.
    desc->transport_type = NS_JINGLE_ICE_UDP;
  } else if (offer && offer->transport_type == NS_JINGLE_ICE_UDP &&
             offer->HasOption(ICE_OPTION_GICE) &&
             protocol_ == ICEPROTO_GOOGLE) {
    desc->transport_type = NS_GINGLE_P2P;
    // Offer is hybrid, we support GICE: use GICE.
  } else if ((!offer || offer->transport_type == NS_GINGLE_P2P) &&
           (protocol_ == ICEPROTO_HYBRID || protocol_ == ICEPROTO_GOOGLE)) {
    // Offer is GICE, we support hybrid or GICE: use GICE.
    desc->transport_type = NS_GINGLE_P2P;
  } else {
    // Mismatch.
    LOG(LS_WARNING) << "Failed to create TransportDescription answer "
                       "because of incompatible transport types";
    return NULL;
  }

  // Generate the ICE credentials if we don't already have them.
  if (!current_description) {
    desc->ice_ufrag = talk_base::CreateRandomString(ICE_UFRAG_LENGTH);
    desc->ice_pwd = talk_base::CreateRandomString(ICE_PWD_LENGTH);
  } else {
    desc->ice_ufrag = current_description->ice_ufrag;
    desc->ice_pwd = current_description->ice_pwd;
  }

  // Negotiate security params.
  if (offer && offer->identity_fingerprint.get()) {
    // The offer supports DTLS, so answer with DTLS, as long as we support it.
    if (secure_ == SEC_ENABLED || secure_ == SEC_REQUIRED) {
      // Fail if we can't create the fingerprint.
      if (!CreateIdentityDigest(desc.get())) {
        return NULL;
      }
    }
  } else if (secure_ == SEC_REQUIRED) {
    // We require DTLS, but the other side didn't offer it. Fail.
    LOG(LS_WARNING) << "Failed to create TransportDescription answer "
                       "because of incompatible security settings";
    return NULL;
  }

  return desc.release();
}

bool TransportDescriptionFactory::CreateIdentityDigest(
    TransportDescription* desc) const {
  if (!identity_) {
    LOG(LS_ERROR) << "Cannot create identity digest with no identity";
    return false;
  }

  desc->identity_fingerprint.reset(
      talk_base::SSLFingerprint::Create(digest_alg_, identity_));
  if (!desc->identity_fingerprint.get()) {
    LOG(LS_ERROR) << "Failed to create identity digest, alg=" << digest_alg_;
    return false;
  }

  return true;
}

}  // namespace cricket

