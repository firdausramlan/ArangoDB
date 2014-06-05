////////////////////////////////////////////////////////////////////////////////
/// @brief transaction
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2013 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Jan Steemann
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_TRANSACTION_TRANSACTION_H
#define TRIAGENS_TRANSACTION_TRANSACTION_H 1

#include "Basics/Common.h"
#include "Transaction/State.h"
#include "VocBase/voc-types.h"

namespace triagens {
  namespace transaction {

    class Manager;

// -----------------------------------------------------------------------------
// --SECTION--                                                 class Transaction
// -----------------------------------------------------------------------------

    class Transaction : public State {

      friend class Manager;

// -----------------------------------------------------------------------------
// --SECTION--                                                          typedefs
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief Transaction
////////////////////////////////////////////////////////////////////////////////

      private:
        Transaction (Transaction const&);
        Transaction& operator= (Transaction const&);

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief create a transaction
////////////////////////////////////////////////////////////////////////////////

        Transaction (Manager*,
                     TRI_voc_tid_t,
                     bool,
                     bool = false);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a transaction
////////////////////////////////////////////////////////////////////////////////

        ~Transaction ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief get the transaction id
////////////////////////////////////////////////////////////////////////////////

        inline TRI_voc_tid_t id () const {
          return _id;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the transaction id for writing it into a marker
////////////////////////////////////////////////////////////////////////////////

        inline TRI_voc_tid_t idForMarker () const {
          if (_singleOperation) {
            return 0;
          }
          return id();
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief is single operation?
////////////////////////////////////////////////////////////////////////////////

        inline bool singleOperation () const {
          return _singleOperation;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief is synchronous?
////////////////////////////////////////////////////////////////////////////////

        inline bool waitForSync () const {
          return _waitForSync;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the transaction start time stamp
////////////////////////////////////////////////////////////////////////////////

        inline double startTime () const {
          return _startTime;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the time since transaction start
////////////////////////////////////////////////////////////////////////////////

        inline double elapsedTime () const {
          if (state() == State::StateType::BEGUN) {
            return TRI_microtime() - _startTime;
          }
          return 0.0;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief begin a transaction
////////////////////////////////////////////////////////////////////////////////

        int begin ();

////////////////////////////////////////////////////////////////////////////////
/// @brief commit a transaction
////////////////////////////////////////////////////////////////////////////////
        
        int commit (bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief rollback a transaction
////////////////////////////////////////////////////////////////////////////////
        
        int rollback ();

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief the transaction manager
////////////////////////////////////////////////////////////////////////////////
        
        Manager* _manager; 

////////////////////////////////////////////////////////////////////////////////
/// @brief transaction id
////////////////////////////////////////////////////////////////////////////////

        TRI_voc_tid_t const _id;

////////////////////////////////////////////////////////////////////////////////
/// @brief whether or not the transaction consists of a single operation
////////////////////////////////////////////////////////////////////////////////

// TODO: do we need this?
        bool const _singleOperation;

////////////////////////////////////////////////////////////////////////////////
/// @brief whether or not the transaction is synchronous
////////////////////////////////////////////////////////////////////////////////

// TODO: do we need this?
        bool _waitForSync;

////////////////////////////////////////////////////////////////////////////////
/// @brief timestamp of transaction start
////////////////////////////////////////////////////////////////////////////////

        double _startTime;

    };

// -----------------------------------------------------------------------------
// --SECTION--                                                   TransactionInfo
// -----------------------------------------------------------------------------
    
    // TODO: move to separate file
    struct TransactionInfo {
      TransactionInfo (TRI_voc_tid_t id, 
                       double elapsedTime) 
        : _id(id),
          _elapsedTime(elapsedTime) {
      }

      TransactionInfo () 
        : _id(0),
          _elapsedTime(0.0) {
      }

      TRI_voc_tid_t const  _id;
      double const         _elapsedTime;
    };

  }
}

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End: