////////////////////////////////////////////////////////////////////////////////
/// @brief Aql, execution engine
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_AQL_EXECUTION_ENGINE_H
#define ARANGODB_AQL_EXECUTION_ENGINE_H 1

#include "Basics/Common.h"

#include "arangod/Aql/AqlItemBlock.h"
#include "arangod/Aql/ExecutionBlock.h"
#include "arangod/Aql/ExecutionPlan.h"
#include "arangod/Aql/ExecutionStats.h"
#include "arangod/Aql/QueryRegistry.h"
#include "Utils/AqlTransaction.h"

namespace triagens {
  namespace aql {

// -----------------------------------------------------------------------------
// --SECTION--                                             class ExecutionEngine
// -----------------------------------------------------------------------------

    class ExecutionEngine {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief create the engine
////////////////////////////////////////////////////////////////////////////////

        ExecutionEngine (Query* query);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the engine, frees all assigned blocks
////////////////////////////////////////////////////////////////////////////////

        ~ExecutionEngine ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
// @brief whether or not we are a coordinator
////////////////////////////////////////////////////////////////////////////////
       
        static bool isCoordinator ();

////////////////////////////////////////////////////////////////////////////////
// @brief whether or not we are a DB server
////////////////////////////////////////////////////////////////////////////////
       
        static bool isDBServer ();

////////////////////////////////////////////////////////////////////////////////
// @brief create an execution engine from a plan
////////////////////////////////////////////////////////////////////////////////

        static ExecutionEngine* instanciateFromPlan (QueryRegistry*, 
                                                     Query*,
                                                     ExecutionPlan*,
                                                     bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief get the root block 
////////////////////////////////////////////////////////////////////////////////
        
        ExecutionBlock* root () const {
          TRI_ASSERT(_root != nullptr);
          return _root;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief set the root block 
////////////////////////////////////////////////////////////////////////////////
        
        void root (ExecutionBlock* root) {
          TRI_ASSERT(root != nullptr);
          _root = root;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the query
////////////////////////////////////////////////////////////////////////////////
        
        Query* getQuery () const {
          return _query;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief initializeCursor, could be called multiple times
////////////////////////////////////////////////////////////////////////////////

        int initializeCursor (AqlItemBlock* items, size_t pos) {
          return _root->initializeCursor(items, pos);
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief shutdown, will be called exactly once for the whole query
////////////////////////////////////////////////////////////////////////////////

        int shutdown (int errorCode) {
          if (_root != nullptr && ! _wasShutdown) {

            // Take care of locking prevention measures in the cluster:
            if (_lockedShards != nullptr) {
              if (triagens::arango::Transaction::_makeNolockHeaders == 
                  _lockedShards) {
                triagens::arango::Transaction::_makeNolockHeaders 
                    = _previouslyLockedShards;
              }
              delete _lockedShards;
              _lockedShards = nullptr;
              _previouslyLockedShards = nullptr;
            }

            // prevent a duplicate shutdown
            int res = _root->shutdown(errorCode);
            _wasShutdown = true;

            return res;
          }

          return TRI_ERROR_NO_ERROR;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief getSome
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock* getSome (size_t atLeast, size_t atMost) {
          return _root->getSome(atLeast, atMost);
        }
        
////////////////////////////////////////////////////////////////////////////////
/// @brief skipSome
////////////////////////////////////////////////////////////////////////////////

        size_t skipSome (size_t atLeast, size_t atMost) {
          return _root->skipSome(atLeast, atMost);
        }
        
////////////////////////////////////////////////////////////////////////////////
/// @brief getOne
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock* getOne () {
          return _root->getSome(1, 1);
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief skip
////////////////////////////////////////////////////////////////////////////////

        bool skip (size_t number) {
          return _root->skip(number);
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief hasMore
////////////////////////////////////////////////////////////////////////////////

        inline bool hasMore () const {
          return _root->hasMore();
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief count
////////////////////////////////////////////////////////////////////////////////

        inline int64_t count () const {
          return _root->count();
        }
        
////////////////////////////////////////////////////////////////////////////////
/// @brief remaining
////////////////////////////////////////////////////////////////////////////////

        inline int64_t remaining () const {
          return _root->remaining();
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief add a block to the engine
////////////////////////////////////////////////////////////////////////////////

        void addBlock (ExecutionBlock*);

// -----------------------------------------------------------------------------
// --SECTION--                                                  public variables
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief execution statistics for the query
/// note that the statistics are modification by execution blocks
////////////////////////////////////////////////////////////////////////////////

        ExecutionStats               _stats;

////////////////////////////////////////////////////////////////////////////////
/// @brief _lockedShards
////////////////////////////////////////////////////////////////////////////////

        std::unordered_set<std::string>* lockedShards () {
          return _lockedShards;
        }
        
// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief all blocks registered, used for memory management
////////////////////////////////////////////////////////////////////////////////

        std::vector<ExecutionBlock*> _blocks;

////////////////////////////////////////////////////////////////////////////////
/// @brief root block of the engine
////////////////////////////////////////////////////////////////////////////////

        ExecutionBlock*              _root;

////////////////////////////////////////////////////////////////////////////////
/// @brief a pointer to the query
////////////////////////////////////////////////////////////////////////////////

        Query*                       _query;

////////////////////////////////////////////////////////////////////////////////
/// @brief whether or not shutdown() was executed
////////////////////////////////////////////////////////////////////////////////

        bool                         _wasShutdown;

////////////////////////////////////////////////////////////////////////////////
/// @brief _previouslyLockedShards, this is read off at instanciating
/// time from a thread local variable
////////////////////////////////////////////////////////////////////////////////

        std::unordered_set<std::string>* _previouslyLockedShards;

////////////////////////////////////////////////////////////////////////////////
/// @brief _lockedShards, these are the shards we have locked for our query
////////////////////////////////////////////////////////////////////////////////

        std::unordered_set<std::string>* _lockedShards;

    };

  }
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
