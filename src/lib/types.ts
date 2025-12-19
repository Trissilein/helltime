export type ScheduleType = "helltide" | "legion" | "world_boss";

export type ScheduleItemBase = {
  id: number;
  timestamp: number; // seconds
  startTime: string; // ISO
  type: ScheduleType;
};

export type WorldBossScheduleItem = ScheduleItemBase & {
  type: "world_boss";
  boss: string;
  zone?: Array<{
    id: string;
    name: string;
    isWhisper: boolean;
    boss?: string;
  }>;
};

export type LegionScheduleItem = ScheduleItemBase & {
  type: "legion";
};

export type HelltideScheduleItem = ScheduleItemBase & {
  type: "helltide";
};

export type ScheduleResponse = {
  world_boss: WorldBossScheduleItem[];
  legion: LegionScheduleItem[];
  helltide: HelltideScheduleItem[];
};

