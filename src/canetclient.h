#ifndef CANETCLIENT_H
#define CANETCLIENT_H

#include "cl1compat.h"
#include "cl1compat.h"
#include "player.h"

/** Net client for multiplayer games.
    @author Andrew Mustun
*/
class CANetClient {
public:
    CANetClient( Player* player );
    ~CANetClient();

    bool joinGame();
    void keepUp();

protected:
    //! Pointer to joined net game or NULL.
    //CL_NetGame* netGame;
    //! Pointer to playing player.
    //Player* player;
}
;

#endif

// EOF
