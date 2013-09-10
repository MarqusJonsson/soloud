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

#include <stdlib.h> // rand
#include <math.h> // sin
#include "soloud.h"

namespace SoLoud
{
	Soloud::Soloud()
	{
		mScratch = NULL;
		mScratchSize = 0;
		mScratchNeeded = 0;
		mVoice = NULL;
		mVoiceCount = 0;
		mSamplerate = 0;
		mBufferSize = 0;
		mFlags = 0;
		mGlobalVolume = 0;
		mPlayIndex = 0;
		mBackendData = NULL;
		mMutex = NULL;
		mPostClipScaler = 0;
		mLockMutexFunc = NULL;
		mUnlockMutexFunc = NULL;
		mStreamTime = 0;
		mAudioSourceID = 1;
		int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			mFilter[i] = NULL;
			mFilterInstance[i] = NULL;
		}
#ifdef SOLOUD_INCLUDE_FFT
		for (i = 0; i < 512; i++)
		{
			mFFTInput[i] = 0;
		}
		for (i = 0; i < 256; i++)
		{
			mFFTData[i] = 0;
		}
#endif
	}

	Soloud::~Soloud()
	{
		stopAll();
		int i;
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			delete mFilterInstance[i];
		}
		delete[] mScratch;
		delete[] mVoice;
		deinit();
	}

	void Soloud::deinit()
	{
		if (mBackendCleanupFunc)
			mBackendCleanupFunc(this);
		mBackendCleanupFunc = 0;
	}

	void Soloud::init(int aVoices, int aSamplerate, int aBufferSize, int aFlags)
	{
		mGlobalVolume = 1;
		mVoice = new AudioSourceInstance*[aVoices];
		mVoiceCount = aVoices;
		int i;
		for (i = 0; i < aVoices; i++)
		{
			mVoice[i] = 0;
		}
		mSamplerate = aSamplerate;
		mScratchSize = 2048;
		mScratchNeeded = 2048;
		mScratch = new float[mScratchSize * 2];
		mBufferSize = aBufferSize;
		mFlags = aFlags;
		mPostClipScaler = 0.5f;
	}

#ifdef SOLOUD_INCLUDE_FFT
	float * Soloud::calcFFT()
	{
		if (mLockMutexFunc) mLockMutexFunc(mMutex);
		mFFT.fft(mFFTInput, mScratch);

		int i;
		for (i = 0; i < 256; i++)
		{
			float real = mScratch[i];
			float imag = mScratch[i+256];
			mFFTData[i] = sqrt(real*real + imag*imag);
		}
		if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);
		return mFFTData;
	}
#endif

	void Soloud::mix(float *aBuffer, int aSamples)
	{
		float buffertime = aSamples / (float)mSamplerate;
		float globalVolume[2];
		mStreamTime += buffertime;

		globalVolume[0] = mGlobalVolume;
		if (mGlobalVolumeFader.mActive)
		{
			mGlobalVolume = mGlobalVolumeFader.get(mStreamTime);
		}
		globalVolume[1] = mGlobalVolume;

		if (mLockMutexFunc) mLockMutexFunc(mMutex);

		// Process faders. May change scratch size.
		int i;
		for (i = 0; i < mVoiceCount; i++)
		{
			if (mVoice[i] && !(mVoice[i]->mFlags & AudioSourceInstance::PAUSED))
			{
				float volume[2];
				float panl[2];
				float panr[2];

				mVoice[i]->mActiveFader = 0;

				if (mGlobalVolumeFader.mActive > 0)
				{
					mVoice[i]->mActiveFader = 1;
				}

				mVoice[i]->mStreamTime += buffertime;

				if (mVoice[i]->mRelativePlaySpeedFader.mActive > 0)
				{
					float speed = mVoice[i]->mRelativePlaySpeedFader.get(mVoice[i]->mStreamTime);
					setVoiceRelativePlaySpeed(i, speed);
				}

				volume[0] = mVoice[i]->mVolume;
				if (mVoice[i]->mVolumeFader.mActive > 0)
				{
					mVoice[i]->mVolume = mVoice[i]->mVolumeFader.get(mVoice[i]->mStreamTime);
					mVoice[i]->mActiveFader = 1;
				}
				volume[1] = mVoice[i]->mVolume;


				panl[0] = mVoice[i]->mLVolume;
				panr[0] = mVoice[i]->mRVolume;
				if (mVoice[i]->mPanFader.mActive > 0)
				{
					float pan = mVoice[i]->mPanFader.get(mVoice[i]->mStreamTime);
					setVoicePan(i, pan);
					mVoice[i]->mActiveFader = 1;
				}
				panl[1] = mVoice[i]->mLVolume;
				panr[1] = mVoice[i]->mRVolume;

				if (mVoice[i]->mPauseScheduler.mActive)
				{
					mVoice[i]->mPauseScheduler.get(mVoice[i]->mStreamTime);
					if (mVoice[i]->mPauseScheduler.mActive == -1)
					{
						mVoice[i]->mPauseScheduler.mActive = 0;
						setVoicePause(i, 1);
					}
				}

				if (mVoice[i]->mActiveFader)
				{
					mVoice[i]->mFaderVolume[0*2+0] = panl[0] * volume[0] * globalVolume[0];
					mVoice[i]->mFaderVolume[0*2+1] = panl[1] * volume[1] * globalVolume[1];
					mVoice[i]->mFaderVolume[1*2+0] = panr[0] * volume[0] * globalVolume[0];
					mVoice[i]->mFaderVolume[1*2+1] = panr[1] * volume[1] * globalVolume[1];
				}

				if (mVoice[i]->mStopScheduler.mActive)
				{
					mVoice[i]->mStopScheduler.get(mVoice[i]->mStreamTime);
					if (mVoice[i]->mStopScheduler.mActive == -1)
					{
						mVoice[i]->mStopScheduler.mActive = 0;
						stopVoice(i);
					}
				}
			}
		}

		// Resize scratch if needed.
		if (mScratchSize < mScratchNeeded)
		{
			mScratchSize = mScratchNeeded;
			delete[] mScratch;
			mScratch = new float[mScratchSize];
		}
		
		// Clear accumulation buffer
		for (i = 0; i < aSamples*2; i++)
		{
			aBuffer[i] = 0;
		}

		// Accumulate sound sources
		for (i = 0; i < mVoiceCount; i++)
		{
			if (mVoice[i] && !(mVoice[i]->mFlags & AudioSourceInstance::PAUSED))
			{

				float stepratio = mVoice[i]->mSamplerate / mSamplerate;
				float step = 0;
				int j;

				int readsamples = (int)ceil(aSamples * stepratio);

				mVoice[i]->getAudio(mScratch, readsamples);	

				for (j = 0; j < FILTERS_PER_STREAM; j++)
				{
					if (mVoice[i]->mFilter[j])
					{
						mVoice[i]->mFilter[j]->filter(
							mScratch, 
							readsamples, 
							mVoice[i]->mFlags & AudioSourceInstance::STEREO, 
							mVoice[i]->mSamplerate,
							mStreamTime);
					}
				}

				if (mVoice[i]->mActiveFader)
				{
					float lpan = mVoice[i]->mFaderVolume[0];
					float rpan = mVoice[i]->mFaderVolume[2];
					float lpani = (mVoice[i]->mFaderVolume[1] - mVoice[i]->mFaderVolume[0]) / aSamples;
					float rpani = (mVoice[i]->mFaderVolume[3] - mVoice[i]->mFaderVolume[2]) / aSamples;

					if (mVoice[i]->mFlags & AudioSourceInstance::STEREO)
					{
						for (j = 0; j < aSamples; j++, step += stepratio, lpan += lpani, rpan += rpani)
						{
							float s1 = mScratch[(int)floor(step)*2];
							float s2 = mScratch[(int)floor(step)*2+1];
							aBuffer[j * 2 + 0] += s1 * lpan;
							aBuffer[j * 2 + 1] += s2 * rpan;
						}
					}
					else
					{
						for (j = 0; j < aSamples; j++, step += stepratio, lpan += lpani, rpan += rpani)
						{
							float s = mScratch[(int)floor(step)];
							aBuffer[j * 2 + 0] += s * lpan;
							aBuffer[j * 2 + 1] += s * rpan;
						}
					}
				}
				else
				{
					float lpan = mVoice[i]->mLVolume * mVoice[i]->mVolume * mGlobalVolume;
					float rpan = mVoice[i]->mRVolume * mVoice[i]->mVolume * mGlobalVolume;
					if (mVoice[i]->mFlags & AudioSourceInstance::STEREO)
					{
						for (j = 0; j < aSamples; j++, step += stepratio)
						{
							float s1 = mScratch[(int)floor(step)*2];
							float s2 = mScratch[(int)floor(step)*2+1];
							aBuffer[j * 2 + 0] += s1 * lpan;
							aBuffer[j * 2 + 1] += s2 * rpan;
						}
					}
					else
					{
						for (j = 0; j < aSamples; j++, step += stepratio)
						{
							float s = mScratch[(int)floor(step)];
							aBuffer[j * 2 + 0] += s * lpan;
							aBuffer[j * 2 + 1] += s * rpan;
						}
					}
				}

				// clear voice if the sound is over
				if (!(mVoice[i]->mFlags & AudioSourceInstance::LOOPING) && mVoice[i]->hasEnded())
				{
					stopVoice(i);
				}
			}
		}

		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (mFilterInstance[i])
			{
				mFilterInstance[i]->filter(aBuffer, aSamples, 1, (float)mSamplerate, mStreamTime);
			}
		}

		if (mUnlockMutexFunc) mUnlockMutexFunc(mMutex);

		// Clip
		if (mFlags & CLIP_ROUNDOFF)
		{
			for (i = 0; i < aSamples*2; i++)
			{
				float f = aBuffer[i];
				if (f <= -1.65f)
				{
					f = -0.9862875f;
				}
				else
				if (f >= 1.65f)
				{
					f = 0.9862875f;
				}
				else
				{
					f =  0.87f * f - 0.1f * f * f * f;
				}
				aBuffer[i] = f * mPostClipScaler;
			}
		}
		else
		{
			for (i = 0; i < aSamples; i++)
			{
				float f = aBuffer[i];
				if (f < -1.0f)
				{
					f = -1.0f;
				}
				else
				if (f > 1.0f)
				{
					f = 1.0f;
				}
				aBuffer[i] = f * mPostClipScaler;
			}
		}

#ifdef SOLOUD_INCLUDE_FFT
		if (mFlags & ENABLE_FFT)
		{
			if (aSamples > 511)
			{
				for (i = 0; i < 512; i++)
				{
					mFFTInput[i] = aBuffer[i*2+0] + aBuffer[i*2+1];
				}
			}
			else
			{
				for (i = 0; i < 512; i++)
				{
					mFFTInput[i] = aBuffer[((i % aSamples) * 2) + 0] + aBuffer[((i % aSamples) * 2) + 1];
				}
			}
		}
#endif
	}
};
