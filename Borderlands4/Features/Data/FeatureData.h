#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <atomic>

namespace Features
{
	struct BulletTracer
	{
		std::vector<SDK::FVector> Points;
		float CreationTime;
		int32 Seed;    // Used to match hits from Server_HitscanHit
		bool bClosed;  // Whether we've already reached MaxPoints or finished
	};

	struct BoneListConfig
	{
		std::string HeadBone = "Head";
		std::string NeckBone = "Neck";
		std::string ChestBone = "Spine3";
		std::string StomachBone = "Spine2";
		std::string PelvisBone = "Hips";
		std::string LeftShoulderBone = "L_Upperarm";
		std::string LeftElbowBone = "L_Forearm";
		std::string LeftHandBone = "L_Hand";
		std::string RightShoulderBone = "R_Upperarm";
		std::string RightElbowBone = "R_Forearm";
		std::string RightHandBone = "R_Hand";
		std::string LeftThighBone = "L_Thigh";
		std::string LeftShinBone = "L_Shin";
		std::string LeftFootBone = "L_Foot";
		std::string RightThighBone = "R_Thigh";
		std::string RightShinBone = "R_Shin";
		std::string RightFootBone = "R_Foot";
	};

	namespace Data
	{
		inline std::mutex TracerMutex;
		inline std::vector<BulletTracer> BulletTracers;
		inline BoneListConfig BoneList;
		
		inline SDK::FVector AimbotTargetPos;
		inline bool bHasAimbotTarget = false;
		inline std::atomic<bool> bTriggerSuppressMouseInput = false;
	}
}
