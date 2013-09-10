/*
SoLoud audio engine
Copyright (c) 2013 Jari Komppa

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#include "soloud.h"

// Core "basic" operations - play, stop, etc

namespace SoLoud
{
	int Soloud::play(AudioSource &aSound, float aVolume, float aPan, int aPaused)
	{
		if (mLockMutexFunc) mLockMutexFunc(mMutex);
		int ch = findFreeVoice();
		if (ch < 0) 
		{
			if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
			return -1;
		}
		if (!aSound.mAudioSourceID)
		{
			aSound.mAudioSourceID = mAudioSourceID;
			mAudioSourceID++;
			aSound.mSoloud = this;
		}
		mVoice[ch] = aSound.createInstance();
		mVoice[ch]->mAudioSourceID = aSound.mAudioSourceID;
		int handle = ch | (mPlayIndex << 12);

		mVoice[ch]->init(mPlayIndex, aSound.mBaseSamplerate, aSound.mFlags);
		
		if (aPaused)
		{
			mVoice[ch]->mFlags |= AudioSourceInstance::PAUSED;
		}

		setVoicePan(ch, aPan);
		setVoiceVolume(ch, aVolume);
		setVoiceRelativePlaySpeed(ch, 1);

		int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (aSound.mFilter[i])
			{
				mVoice[ch]->mFilter[i] = aSound.mFilter[i]->createInstance();
			}
		}

		mPlayIndex++;
		int scratchneeded = (int)ceil((mVoice[ch]->mSamplerate / mSamplerate) * mBufferSize);
		if (mScratchNeeded < scratchneeded)
		{
			int pot = 1024;
			while (pot < scratchneeded) pot <<= 1;
			mScratchNeeded = pot;
		}
		if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
		return handle;
	}	

	void Soloud::seek(int aVoiceHandle, float aSeconds)
	{
		if (mLockMutexFunc) mLockMutexFunc(mMutex);
		int ch = getVoiceFromHandle(aVoiceHandle);
		if (ch == -1) 
		{
			if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
			return;
		}
		mVoice[ch]->seek(aSeconds, mScratch, mScratchSize);
		if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
	}


	void Soloud::stop(int aVoiceHandle)
	{
		int ch = getVoiceFromHandle(aVoiceHandle);
		if (ch == -1) 
		{
			return;
		}
		if (mLockMutexFunc) mLockMutexFunc(mMutex);
		stopVoice(ch);
		if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
	}

	void Soloud::stopSound(AudioSource &aSound)
	{
		if (aSound.mAudioSourceID)
		{
			if (mLockMutexFunc) mLockMutexFunc(mMutex);
			
			int i;
			for (i = 0; i < mVoiceCount; i++)
			{
				if (mVoice[i] && mVoice[i]->mAudioSourceID == aSound.mAudioSourceID)
				{
					stopVoice(i);
				}
			}
			if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
		}
	}

	void Soloud::stopAll()
	{
		int i;
		if (mLockMutexFunc) mLockMutexFunc(mMutex);
		for (i = 0; i < mVoiceCount; i++)
		{
			stopVoice(i);
		}
		if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
	}
}
