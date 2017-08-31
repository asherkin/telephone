# Telephone

## Game Server

Construct Origin: header as <gamedir>://<ip>:<port>

Telephone instance can be bound to 

### Connecting to Exchange
Game server connects to configured Exchange.

### Listening for Connections

## Exchange

MySQL backing store.
Websocket servers can be sharded by gameserver.
Clients can connect to an exchange session even if no gameserver connected, but gameserver needs to be known.
Clients make a request via authenticated HTTP to get websocket URI to connect to including auth token as query param / basic auth username.
wss://3490kdfg09k32sdflkn234@broker001.telephone.limetech.io/server/2346


### Production

### Development

## Client

### Connecting to Exchange

### Connecting to Server

