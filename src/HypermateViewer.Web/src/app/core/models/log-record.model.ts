export interface LogRecord {
  dateId: number;
  eventTime: string;
  line: string;
  machine: string;
  stepDescription: string;
  oldState: string;
  newState: string;
  oldErpCode: string;
  newErpCode: string;
}
