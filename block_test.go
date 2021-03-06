/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

package reftable

import (
	"crypto/sha1"
	"fmt"
	"testing"
)

func TestBlockSeekLog(t *testing.T) {
	testBlockSeek(t, blockTypeLog)
}

func TestBlockSeekRef(t *testing.T) {
	testBlockSeek(t, blockTypeRef)
}

func createSeekReader(t *testing.T, typ byte, bs uint32) ([]string, *blockReader) {
	block := make([]byte, bs)

	const headerOff = 17
	bw := newBlockWriter(typ, block, headerOff, sha1.Size)

	var names []string
	N := 30
	for i := 0; i < N; i++ {
		names = append(names, fmt.Sprintf("refs/heads/branch%02d", i))
	}
	for i, n := range names {
		var rec record
		if typ == blockTypeRef {
			rec = &RefRecord{
				RefName: n,
			}
		} else if typ == blockTypeLog {
			rec = &LogRecord{
				RefName: n,
				Message: "hello",
				Old:     testHash(1),
				New:     testHash(2),
			}
		}

		names[i] = rec.key()
		bw.add(rec)
	}

	block = bw.finish()

	br, err := newBlockReader(block, headerOff, bs, sha1.Size)
	if err != nil {
		t.Fatalf("newBlockReader: %v", err)
	}
	return names, br
}

func testBlockSeek(t *testing.T, typ byte) {
	bs := uint32(10240)

	names, br := createSeekReader(t, typ, bs)

	for _, nm := range names {
		bi, err := br.seek(nm)
		if err != nil {
			t.Fatalf("Seek %q: %v", nm, err)
		}

		res := newRecord(typ, "")
		ok, err := bi.Next(res)
		if err != nil {
			t.Fatalf("next: %v", err)
		}
		if !ok {
			t.Fatalf("next: should have returned true")
		}

		if res.key() != nm {
			t.Errorf("got %q want %q", res.key(), nm)
		}
	}
}

func TestBlockSeekPrefixLog(t *testing.T) {
	testBlockSeekPrefix(t, blockTypeLog)
}

func TestBlockSeekPrefixRef(t *testing.T) {
	testBlockSeekPrefix(t, blockTypeRef)
}

func testBlockSeekPrefix(t *testing.T, typ byte) {
	bs := uint32(10240)

	names, br := createSeekReader(t, typ, bs)

	nm := names[10]
	nm = nm[:len(nm)-1]
	bi, err := br.seek(nm)
	if err != nil {
		t.Fatalf("Seek %q: %v", nm, err)
	}

	res := newRecord(typ, "")
	ok, err := bi.Next(res)
	if err != nil {
		t.Fatalf("next: %v", err)
	}
	if !ok {
		t.Fatalf("next: should have returned true")
	}

	want := names[10]
	if res.key() != want {
		t.Errorf("got %q want %q", res.key(), want)
	}

}

func testBlockSeekLast(t *testing.T, typ byte) {
	bs := uint32(10240)

	names, br := createSeekReader(t, typ, bs)

	nm := names[len(names)-1] + "z"
	bi, err := br.seek(nm)
	if err != nil {
		t.Fatalf("Seek %q: %v", nm, err)
	}

	res := newRecord(typ, "")
	ok, err := bi.Next(res)
	if err != nil {
		t.Fatalf("Next %q: %v", nm, err)
	}
	if ok {
		t.Fatalf("got record %q, expected end of block", res.key())
	}
}

func TestBlockSeekLastRef(t *testing.T) {
	testBlockSeekLast(t, blockTypeRef)
}

func TestBlockSeekLastLog(t *testing.T) {
	testBlockSeekLast(t, blockTypeLog)
}

func testBlockSeekFirst(t *testing.T, typ byte) {
	bs := uint32(10240)

	names, br := createSeekReader(t, typ, bs)

	bi, err := br.seek("")
	if err != nil {
		t.Fatalf("Seek %q: %v", "", err)
	}

	res := newRecord(typ, "")
	ok, err := bi.Next(res)
	if err != nil || !ok {
		t.Fatalf("Next: %v, %v", ok, err)
	}

	if res.key() != names[0] {
		t.Fatalf("got %q, want key %q", res.key(), names[0])
	}
}

func TestBlockSeekFirstRef(t *testing.T) {
	testBlockSeekFirst(t, blockTypeRef)
}

func TestBlockSeekFirstLog(t *testing.T) {
	testBlockSeekFirst(t, blockTypeLog)
}

func readIter(typ byte, bi iterator) ([]record, error) {
	var result []record
	for {
		rec := newRecord(typ, "")
		ok, err := bi.Next(rec)

		if err != nil {
			return result, err
		}

		if !ok {
			break
		}
		result = append(result, rec)

	}
	return result, nil
}

func TestBlockRestart(t *testing.T) {
	block := make([]byte, 512)
	const headerOff = 17
	bw := newBlockWriter(blockTypeRef, block, headerOff, sha1.Size)
	rec := &RefRecord{
		RefName: "refs/heads/master",
	}
	bw.add(rec)

	finished := bw.finish()

	br, err := newBlockReader(finished, headerOff, 512, sha1.Size)
	if err != nil {
		t.Fatalf("newBlockReader: %v", err)
	}

	if br.restartOffset(0) != headerOff+4 {
		t.Fatalf("expected a restart at 4")
	}
	rkey, err := decodeRestartKey(block, br.restartOffset(0))
	if err != nil {
		t.Fatalf("decodeRestartKey %v", err)
	}
	if rkey != rec.RefName {
		t.Fatalf("got %q, want %q", rkey, rec.RefName)
	}
}

func TestBlockPadding(t *testing.T) {
	block := make([]byte, 512)
	const headerOff = 17

	bw := newBlockWriter(blockTypeRef, block, headerOff, sha1.Size)
	rec := &RefRecord{
		RefName: "refs/heads/master",
	}
	bw.add(rec)

	finished := bw.finish()

	br, err := newBlockReader(finished, headerOff, 512, sha1.Size)
	if err != nil {
		t.Fatalf("newBlockReader: %v", err)
	}

	var bi blockIter
	br.start(&bi)
	res, err := readIter(bi.br.getType(), &bi)
	if err != nil {
		t.Fatalf("readBlockIter: %v", err)
	}
	if len(res) != 1 {
		t.Fatalf("got %v, want len 1", res)
	}
}

func TestBlockHeader(t *testing.T) {
	blockSize := 512
	block := make([]byte, blockSize)
	header := "hello"
	copy(block, header)
	bw := newBlockWriter(blockTypeRef, block, uint32(len(header)), sha1.Size)

	name := "refs/heads/master"
	rec := &RefRecord{
		RefName: name,
	}
	bw.add(rec)

	block = bw.finish()

	if got := string(block[:len(header)]); got != header {
		t.Fatalf("got header %q want %q", got, header)
	}
	_, err := newBlockReader(block, uint32(len(header)), uint32(blockSize), sha1.Size)
	if err != nil {
		t.Fatalf("newBlockReader: %v", err)
	}
}
