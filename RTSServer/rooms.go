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
)

type Room struct {
	ID        string   `json:"id"`
	PlayerIDs []string `json:"player_ids"`
}

type kickoffState struct {
	ID       uint32
	Deadline time.Time
	Acked    map[string]bool
}

type storedRoom struct {
	ID            string
	PlayerIDs     []string
	Addrs         map[string]*net.UDPAddr
	Ready         map[string]struct{}
	Kickoff       *kickoffState
	nextKickoffID uint32
}

type KickoffBroadcast struct {
	RoomID      string
	KickoffID   uint32
	RemainingMs uint32
	Addrs       []*net.UDPAddr
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
		Addrs:     map[string]*net.UDPAddr{},
		Ready:     map[string]struct{}{},
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
func (s *RoomStore) Seat(roomID, playerID string, addr *net.UDPAddr) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return Room{}, fmt.Errorf("room not found")
	}
	if !contains(room.PlayerIDs, playerID) {
		room.PlayerIDs = append(room.PlayerIDs, playerID)
		sort.Strings(room.PlayerIDs)
	}
	if addr != nil {
		copied := *addr
		room.Addrs[playerID] = &copied
	}
	return snapshotRoom(room), nil
}

func (s *RoomStore) RelayAddrs(roomID, senderID string) ([]*net.UDPAddr, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return nil, fmt.Errorf("room not found")
	}
	if !contains(room.PlayerIDs, senderID) {
		return nil, fmt.Errorf("player not in room")
	}
	return copyAddrs(room), nil
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
	if room.Kickoff != nil {
		delete(room.Kickoff.Acked, playerID)
		if len(room.PlayerIDs) == 0 {
			room.Kickoff = nil
		}
	}
	return snapshotRoom(room), nil
}

func snapshotRoom(room *storedRoom) Room {
	players := make([]string, len(room.PlayerIDs))
	copy(players, room.PlayerIDs)
	return Room{ID: room.ID, PlayerIDs: players}
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
