
/* Copyright (c) <2009> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/

#include "stdafx.h"
#include "dTimeTracker.h"

#if defined (D_TIME_TRACKER) && defined(_MSC_VER)

dTimeTracker* dTimeTracker::GetInstance()
{
	static dTimeTracker instance;
	return &instance;
}


dTimeTracker::dTrackerThread::dTrackerThread (const char* const name)
	:m_name (name)
	,m_buffer ((dTrackRecord*) dContainersAlloc::Alloc (1024 * sizeof (dTrackRecord)))
	,m_index (0)
	,m_size (1024)
	,m_threadId (GetCurrentThreadId())
	,m_processId (GetCurrentProcessId())
{
}

dTimeTracker::dTrackerThread::~dTrackerThread ()
{
	dContainersAlloc::Free (m_buffer);
}

void dTimeTracker::dTrackerThread::Realloc()
{
	dTrackRecord* const buffer = ((dTrackRecord*) dContainersAlloc::Alloc (2 * m_size * sizeof (dTrackRecord)));
	memcpy (m_buffer, buffer, m_size * sizeof (dTrackRecord));
	m_size = m_size * 2;
}



dTimeTracker::dTimeTrackerEntry::dTimeTrackerEntry(dCRCTYPE nameCRC)
{
	dTimeTracker* const instance = dTimeTracker::GetInstance();
	if (instance->m_file) {
		long long startTime = instance->GetTimeInMicrosenconds ();

		long long threadId = GetCurrentThreadId();
		dList<dTimeTracker::dTrackerThread>::dListNode* tracker = instance->m_tracks.GetFirst();
		for (dList<dTimeTracker::dTrackerThread>::dListNode* threadPtr = instance->m_tracks.GetFirst(); threadPtr; threadPtr = threadPtr->GetNext()) {
			if (threadPtr->GetInfo().m_threadId == threadId) {
				tracker = threadPtr;
				break;
			}
		}
	
		dTrackerThread& thread = tracker->GetInfo();
		m_thread = &thread;
		thread.m_stack.Append (this);

		if (thread.m_index >= thread.m_size) {
			thread.Realloc();
		}

		m_index = thread.m_index;
		dTrackRecord& record = thread.m_buffer[thread.m_index];
		record.m_nameCRC = nameCRC;
		record.m_startTime = startTime;
		record.m_size = thread.m_index;
		thread.m_index ++;
	}
}

dTimeTracker::dTimeTrackerEntry::~dTimeTrackerEntry()
{
	dTimeTracker* const instance = dTimeTracker::GetInstance();
	if (instance->m_file) {
		dAssert (this == m_thread->m_stack.GetLast()->GetInfo());
		m_thread->m_stack.Remove (m_thread->m_stack.GetLast());

		dTrackRecord& record = m_thread->m_buffer[m_index];
		record.m_size = m_thread->m_index - record.m_size;
		record.m_endTime = instance->GetTimeInMicrosenconds ();

		if (!m_thread->m_stack.GetCount()) {
			instance->WriteTrack (m_thread, record);
			m_thread->m_index = 0;
		}
	}
}


dTimeTracker::dTimeTracker ()
	:m_file(NULL)
	,m_firstRecord(false)
{
	StartSection ("xxxx.json");

	QueryPerformanceFrequency(&m_frequency);
	QueryPerformanceCounter (&m_baseCount);
}

dTimeTracker::~dTimeTracker ()
{
	EndSection ();
}


void dTimeTracker::StartSection (const char* const fileName)
{
	if (m_file) {
		fclose (m_file);
	}

	m_file = fopen (fileName, "wb");

	fprintf (m_file, "{\n");
	fprintf (m_file, "\t\"traceEvents\": [\n");
}

void dTimeTracker::EndSection ()
{
	fprintf (m_file, "\n");
	fprintf (m_file, "\t],\n");

	fprintf (m_file, "\t\"displayTimeUnit\": \"ns\",\n");
	fprintf (m_file, "\t\"systemTraceEvents\": \"SystemTraceData\",\n");
	fprintf (m_file, "\t\"otherData\": {\n");
	fprintf (m_file, "\t\t\"version\": \"My Application v1.0\"\n");
	fprintf (m_file, "\t}\n");
	fprintf (m_file, "}\n");

	fclose (m_file);
	m_file = NULL;
}


void dTimeTracker::CreateTrack (const char* const name)
{
	m_tracks.Append (dTrackerThread (name));
}

dCRCTYPE dTimeTracker::RegisterName (const char* const name)
{
	dCRCTYPE crc = dCRC64 (name);
	m_dictionary.Insert (name, crc);
	return crc;
}

long long dTimeTracker::GetTimeInMicrosenconds()
{
	LARGE_INTEGER count;
	QueryPerformanceCounter (&count);
	count.QuadPart -= m_baseCount.QuadPart;
	LONGLONG ticks = LONGLONG (count.QuadPart * LONGLONG (1000000) / m_frequency.QuadPart);
	return ticks;
}

/*
{
  "traceEvents": [
    {"name": "Asub", "cat": "xxxxx", "ph": "B", "pid": 22630, "tid": 22630, "ts": 829},
    {"name": "Asub", "cat": "xxxxx", "ph": "E", "pid": 22630, "tid": 22630, "ts": 833}
  ],

  "displayTimeUnit": "ns",
  "systemTraceEvents": "SystemTraceData",
  "otherData": {
    "version": "My Application v1.0"
  }
}
*/

void dTimeTracker::WriteTrack (const dTrackerThread* const track, const dTrackRecord& record)
{

//  {"name": "Asub", "cat": "xxxxx", "ph": "B", "pid": 22630, "tid": 22630, "ts": 829}
	

	const dTrackRecord* ptr = &record;
	for (int i = 0; i < record.m_size; i ++) {

		if (!m_firstRecord) {
			fprintf (m_file, "\n");
			m_firstRecord = true;
		} else {
			fprintf (m_file, ",\n");
		}

		fprintf (m_file, "\t\t {");
		fprintf (m_file, "\"name\": \"%s\", ", m_dictionary.Find (ptr->m_nameCRC)->GetInfo());
		fprintf (m_file, "\"cat\": \"%s\", ", track->m_name);
		fprintf (m_file, "\"ph\": \"B\", ");
		fprintf (m_file, "\"pid\": \"%d\", ", track->m_processId);
		fprintf (m_file, "\"tid\": \"%d\", ", track->m_threadId);
		fprintf (m_file, "\"ts\": %d", ptr->m_startTime);
		fprintf (m_file, "},\n");

		fprintf (m_file, "\t\t {");
		fprintf (m_file, "\"name\": \"%s\", ", m_dictionary.Find (ptr->m_nameCRC)->GetInfo());
		fprintf (m_file, "\"cat\": \"%s\", ", track->m_name);
		fprintf (m_file, "\"ph\": \"E\", ");
		fprintf (m_file, "\"pid\": \"%d\", ", track->m_processId);
		fprintf (m_file, "\"tid\": \"%d\", ", track->m_threadId);
		fprintf (m_file, "\"ts\": %d", ptr->m_endTime);
		fprintf (m_file, "}");
		
		ptr ++;
	}
}

#endif