package main

import (
	"fmt"
	"net"
	"sort"
	"sync"
	"time"
)

const (
	kickoffDuration = 1000 * time.Millisecond
	kickoffRepeat   = 100 * time.Millisecond
	clickKeep       = 10 * time.Second
)

type Room struct {
	ID        string   `json:"id"`
	PlayerIDs []string `json:"player_ids"`
	Seats     []uint8  `json:"seats"`
}

type kickoffState struct {
	ID       uint32
	Deadline time.Time
	Acked    map[string]bool
}

type cachedClick struct {
	Seat     byte
	OrderID  uint32
	Body     []byte
	Deadline time.Time
}

type storedRoom struct {
	ID            string
	PlayerIDs     []string
	Seats         map[string]byte
	nextSeat      uint16
	Addrs         map[string]*net.UDPAddr
	Ready         map[string]struct{}
	Acks          map[string]map[byte]uint32
	Clicks        []cachedClick
	Kickoff       *kickoffState
	nextKickoffID uint32
}

type KickoffBroadcast struct {
	RoomID      string
	KickoffID   uint32
	RemainingMs uint32
	Addrs       []*net.UDPAddr
}

type destBounce struct {
	Addr   *net.UDPAddr
	Packet []byte
}

type RoomStore struct {
	mu    sync.Mutex
	rooms map[string]*storedRoom
}

func NewRoomStore() *RoomStore {
	return &RoomStore{rooms: make(map[string]*storedRoom)}
}

func (s *RoomStore) List() []Room {
	s.mu.Lock()
	defer s.mu.Unlock()

	out := make([]Room, 0, len(s.rooms))
	for _, room := range s.rooms {
		out = append(out, snapshotRoom(room))
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

func (s *RoomStore) Create(id string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.rooms[id]; exists {
		return Room{}, fmt.Errorf("room already exists")
	}
	room := &storedRoom{
		ID:        id,
		PlayerIDs: []string{},
		Seats:     map[string]byte{},
		Addrs:     map[string]*net.UDPAddr{},
		Ready:     map[string]struct{}{},
		Acks:      map[string]map[byte]uint32{},
	}
	s.rooms[id] = room
	return snapshotRoom(room), nil
}

func (s *RoomStore) Get(id string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[id]
	if !ok {
		return Room{}, fmt.Errorf("room not found")
	}
	return snapshotRoom(room), nil
}

func (s *RoomStore) IsSeated(roomID, playerID string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	room, ok := s.rooms[roomID]
	if !ok {
		return false
	}
	return contains(room.PlayerIDs, playerID)
}

// Seat adds the player on first UDP Hello and maps their datagram address.
// Join order is 0, 1, 2, … and is never reused in that room.
func (s *RoomStore) Seat(roomID, playerID string, addr *net.UDPAddr) (Room, byte, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return Room{}, 0, fmt.Errorf("room not found")
	}
	if !contains(room.PlayerIDs, playerID) {
		if room.nextSeat >= 255 {
			return Room{}, 0, fmt.Errorf("room full")
		}
		seat := byte(room.nextSeat)
		room.nextSeat++
		room.PlayerIDs = append(room.PlayerIDs, playerID)
		room.Seats[playerID] = seat
	}
	if addr != nil {
		copied := *addr
		room.Addrs[playerID] = &copied
	}
	return snapshotRoom(room), room.Seats[playerID], nil
}

func (s *RoomStore) MarkStart(roomID, playerID string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return Room{}, fmt.Errorf("room not found")
	}
	if !contains(room.PlayerIDs, playerID) {
		return Room{}, fmt.Errorf("player not in room")
	}
	room.Ready[playerID] = struct{}{}
	if room.Kickoff == nil && allSeatedReady(room) {
		room.nextKickoffID++
		room.Kickoff = &kickoffState{
			ID:       room.nextKickoffID,
			Deadline: time.Now().Add(kickoffDuration),
			Acked:    map[string]bool{},
		}
	}
	return snapshotRoom(room), nil
}

func (s *RoomStore) AckKickoff(roomID, playerID string, id uint32) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return fmt.Errorf("room not found")
	}
	if !contains(room.PlayerIDs, playerID) {
		return fmt.Errorf("player not in room")
	}
	if room.Kickoff == nil || room.Kickoff.ID != id {
		return nil
	}
	room.Kickoff.Acked[playerID] = true
	return nil
}

func (s *RoomStore) KickoffBroadcasts(now time.Time) []KickoffBroadcast {
	s.mu.Lock()
	defer s.mu.Unlock()

	out := make([]KickoffBroadcast, 0)
	for _, room := range s.rooms {
		if room.Kickoff == nil {
			continue
		}
		remaining := room.Kickoff.Deadline.Sub(now)
		if remaining <= 0 || allKickoffAcked(room) {
			room.Kickoff = nil
			continue
		}
		out = append(out, KickoffBroadcast{
			RoomID:      room.ID,
			KickoffID:   room.Kickoff.ID,
			RemainingMs: uint32(remaining.Milliseconds()),
			Addrs:       copyAddrs(room),
		})
	}
	return out
}

func (s *RoomStore) Leave(roomID, playerID string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return Room{}, fmt.Errorf("room not found")
	}
	next := make([]string, 0, len(room.PlayerIDs))
	found := false
	for _, id := range room.PlayerIDs {
		if id == playerID {
			found = true
			continue
		}
		next = append(next, id)
	}
	if !found {
		return Room{}, fmt.Errorf("player not in room")
	}
	room.PlayerIDs = next
	delete(room.Addrs, playerID)
	delete(room.Ready, playerID)
	delete(room.Acks, playerID)
	if room.Kickoff != nil {
		delete(room.Kickoff.Acked, playerID)
		if len(room.PlayerIDs) == 0 {
			room.Kickoff = nil
		}
	}
	return snapshotRoom(room), nil
}

func (s *RoomStore) PrepareOrderBounces(roomID, senderID string, prefix []byte, cmd orderCommand, acks []ackEntry, piggybacks [][]byte) ([]destBounce, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return nil, fmt.Errorf("room not found")
	}
	if !contains(room.PlayerIDs, senderID) {
		return nil, fmt.Errorf("player not in room")
	}

	now := time.Now()
	pruneClicks(room, now)
	recordAcks(room, senderID, acks)
	if len(cmd.UnitIDs) > 0 {
		cacheClick(room, cmd.Seat, cmd.OrderID, cmd.Body, now)
	}
	for _, body := range piggybacks {
		pb, _, err := parseCommandBody(body, 0)
		if err != nil || len(pb.UnitIDs) == 0 {
			continue
		}
		cacheClick(room, pb.Seat, pb.OrderID, pb.Body, now)
	}

	out := make([]destBounce, 0, len(room.PlayerIDs))
	for _, destID := range room.PlayerIDs {
		addr := room.Addrs[destID]
		if addr == nil {
			continue
		}
		copied := *addr
		packet, err := encodeDestBounce(room, destID, prefix, cmd, now)
		if err != nil {
			continue
		}
		out = append(out, destBounce{Addr: &copied, Packet: packet})
	}
	return out, nil
}

func snapshotRoom(room *storedRoom) Room {
	players := make([]string, len(room.PlayerIDs))
	copy(players, room.PlayerIDs)
	seats := make([]uint8, len(room.PlayerIDs))
	for i, id := range room.PlayerIDs {
		seats[i] = room.Seats[id]
	}
	return Room{ID: room.ID, PlayerIDs: players, Seats: seats}
}

func copyAddrs(room *storedRoom) []*net.UDPAddr {
	out := make([]*net.UDPAddr, 0, len(room.Addrs))
	for _, addr := range room.Addrs {
		if addr == nil {
			continue
		}
		copied := *addr
		out = append(out, &copied)
	}
	return out
}

func allSeatedReady(room *storedRoom) bool {
	if len(room.PlayerIDs) == 0 {
		return false
	}
	for _, id := range room.PlayerIDs {
		if _, ok := room.Ready[id]; !ok {
			return false
		}
	}
	return true
}

func allKickoffAcked(room *storedRoom) bool {
	if room.Kickoff == nil || len(room.PlayerIDs) == 0 {
		return false
	}
	for _, id := range room.PlayerIDs {
		if !room.Kickoff.Acked[id] {
			return false
		}
	}
	return true
}

func contains(ids []string, want string) bool {
	for _, id := range ids {
		if id == want {
			return true
		}
	}
	return false
}

func recordAcks(room *storedRoom, senderID string, acks []ackEntry) {
	row := room.Acks[senderID]
	if row == nil {
		row = map[byte]uint32{}
		room.Acks[senderID] = row
	}
	for _, ack := range acks {
		row[ack.Seat] = ack.Last
	}
}

func cacheClick(room *storedRoom, seat byte, orderID uint32, body []byte, now time.Time) {
	copied := append([]byte(nil), body...)
	for i := range room.Clicks {
		if room.Clicks[i].Seat == seat && room.Clicks[i].OrderID == orderID {
			room.Clicks[i].Body = copied
			room.Clicks[i].Deadline = now.Add(clickKeep)
			return
		}
	}
	room.Clicks = append(room.Clicks, cachedClick{
		Seat:     seat,
		OrderID:  orderID,
		Body:     copied,
		Deadline: now.Add(clickKeep),
	})
}

func pruneClicks(room *storedRoom, now time.Time) {
	kept := room.Clicks[:0]
	for _, click := range room.Clicks {
		if click.Deadline.After(now) {
			kept = append(kept, click)
		}
	}
	room.Clicks = kept
}

func lastAck(room *storedRoom, playerID string, originator byte) uint32 {
	row := room.Acks[playerID]
	if row == nil {
		return 0
	}
	return row[originator]
}

func fullyAcked(room *storedRoom, destID string, originator byte) uint32 {
	var minVal uint32
	found := false
	for _, id := range room.PlayerIDs {
		if id == destID {
			continue
		}
		v := lastAck(room, id, originator)
		if !found || v < minVal {
			minVal = v
			found = true
		}
	}
	if !found {
		return lastAck(room, destID, originator)
	}
	return minVal
}

func encodeDestBounce(room *storedRoom, destID string, prefix []byte, cmd orderCommand, now time.Time) ([]byte, error) {
	destSeat := room.Seats[destID]
	buf := append([]byte(nil), prefix...)
	buf = append(buf, byte(len(room.PlayerIDs)))
	for _, id := range room.PlayerIDs {
		originator := room.Seats[id]
		buf = append(buf, originator)
		buf = appendU32(buf, fullyAcked(room, destID, originator))
	}

	var piggy [][]byte
	for _, click := range room.Clicks {
		if !click.Deadline.After(now) {
			continue
		}
		if click.Seat == destSeat {
			continue
		}
		if len(cmd.UnitIDs) > 0 && click.Seat == cmd.Seat && click.OrderID == cmd.OrderID {
			continue
		}
		if lastAck(room, destID, click.Seat) >= click.OrderID {
			continue
		}
		if len(buf)+1+len(click.Body)+piggySize(piggy) > udpMaxPacket {
			continue
		}
		piggy = append(piggy, click.Body)
	}
	if len(piggy) > 255 {
		piggy = piggy[:255]
	}
	buf = append(buf, byte(len(piggy)))
	for _, body := range piggy {
		buf = append(buf, body...)
	}
	if len(buf) > udpMaxPacket {
		return nil, fmt.Errorf("bounce too large")
	}
	return buf, nil
}

func piggySize(bodies [][]byte) int {
	n := 0
	for _, body := range bodies {
		n += len(body)
	}
	return n
}
