#define TEST_ENABLED 0

#if TEST_ENABLED == 1

#include "../Include/SerDes.h"
#include <assert.h>

int main() {

	sample_object_t test_msg = {0};
	uint8_t buffer[256] = {0};
    return_code_t rc;

    rc = Serialize_Object(NULL, buffer, &SAMPLE_OBJECT_DESCRIPTOR);
    assert(rc == RC_INVALID_ARG);

    rc = Serialize_Object(&test_msg, NULL, &SAMPLE_OBJECT_DESCRIPTOR);
    assert(rc == RC_INVALID_ARG);

    rc = Serialize_Object(&test_msg, buffer, NULL);
    assert(rc == RC_INVALID_ARG);

	test_msg.id = 42;
	test_msg.status = 3.14f;

	test_msg.baro_sensor.temperature = 25.5f;
	test_msg.baro_sensor.pressure = 1013.25f;
	test_msg.baro_sensor.humidity = 40.0f;

	for (int i = 0; i < 8; i++) {
		test_msg.lidar_sensor.obj_dist[i] = (float)(i * 10);
	}
	test_msg.lidar_sensor.altitude = 150.0f;
	for (int i = 0; i < 3; i++) {
		test_msg.lidar_sensor.light_levels[i] = i * 100;
	}
	test_msg.lidar_sensor.altitude = 250.0f;

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			test_msg.imu_sensor[i].attitude[j] = (float)(i + j);
		}
		test_msg.imu_sensor[i].heading = (float)(i * 45);
	}

	for (int i = 0; i < 50; i++) {
		test_msg.message[i] = (uint8_t)(i);
	}

	test_msg.timestamp = 1627847261;

	sample_object_t recv_msg = {0};

	Serialize_Object(&test_msg, buffer, &SAMPLE_OBJECT_DESCRIPTOR);
	Deserialize_Object(buffer, &recv_msg, &SAMPLE_OBJECT_DESCRIPTOR);

	printf("Original ID: %u, Deserialized ID: %u\n", test_msg.id, recv_msg.id);
	printf("Original Status: %.2f, Deserialized Status: %.2f\n", test_msg.status, recv_msg.status);

	printf("Original Baro Temp: %.2f, Deserialized Baro Temp: %.2f\n", test_msg.baro_sensor.temperature, recv_msg.baro_sensor.temperature);
	printf("Original Baro Pressure: %.2f, Deserialized Baro Pressure: %.2f\n", test_msg.baro_sensor.pressure, recv_msg.baro_sensor.pressure);
	printf("Original Baro Humidity: %.2f, Deserialized Baro Humidity: %.2f\n", test_msg.baro_sensor.humidity, recv_msg.baro_sensor.humidity);

	printf("Original Lidar Altitude: %.2f, Deserialized Lidar Altitude: %.2f\n", test_msg.lidar_sensor.altitude, recv_msg.lidar_sensor.altitude);
	for (int i = 0; i < 3; i++) {
		printf("Original Lidar Light Level[%d]: %u, Deserialized Lidar Light Level[%d]: %u\n", i, test_msg.lidar_sensor.light_levels[i], i, recv_msg.lidar_sensor.light_levels[i]);
	}
	for (int i = 0; i < 8; i++) {
		printf("Original Lidar Obj Dist[%d]: %.2f, Deserialized Lidar Obj Dist[%d]: %.2f\n", i, test_msg.lidar_sensor.obj_dist[i], i, recv_msg.lidar_sensor.obj_dist[i]);
	}

	for (int i = 0; i < 3; i++) {
		printf("Original IMU[%d] Heading: %.2f, Deserialized IMU[%d] Heading: %.2f\n", i, test_msg.imu_sensor[i].heading, i, recv_msg.imu_sensor[i].heading);
		for (int j = 0; j < 3; j++) {
			printf("Original IMU[%d] Attitude[%d]: %.2f, Deserialized IMU[%d] Attitude[%d]: %.2f\n", i, j, test_msg.imu_sensor[i].attitude[j], i, j,
				   recv_msg.imu_sensor[i].attitude[j]);
		}
	}

	printf("Original Message Bytes: ");
	for (int i = 0; i < 50; i++) {
		printf("%u ", test_msg.message[i]);
	}
	printf("\nDeserialized Message Bytes: ");
	for (int i = 0; i < 50; i++) {
		printf("%u ", recv_msg.message[i]);
	}
	printf("\n");

	printf("Original Timestamp: %llu, Deserialized Timestamp: %llu\n", test_msg.timestamp, recv_msg.timestamp);

	assert(test_msg.id == recv_msg.id);
	assert(test_msg.status == recv_msg.status);
	assert(test_msg.baro_sensor.temperature == recv_msg.baro_sensor.temperature);
	assert(test_msg.baro_sensor.pressure == recv_msg.baro_sensor.pressure);
	assert(test_msg.baro_sensor.humidity == recv_msg.baro_sensor.humidity);
	assert(test_msg.lidar_sensor.altitude == recv_msg.lidar_sensor.altitude);
	for (int i = 0; i < 3; i++) {
		assert(test_msg.lidar_sensor.light_levels[i] == recv_msg.lidar_sensor.light_levels[i]);
	}
	for (int i = 0; i < 8; i++) {
		assert(test_msg.lidar_sensor.obj_dist[i] == recv_msg.lidar_sensor.obj_dist[i]);
	}
	for (int i = 0; i < 3; i++) {
		assert(test_msg.imu_sensor[i].heading == recv_msg.imu_sensor[i].heading);
		for (int j = 0; j < 3; j++) {
			assert(test_msg.imu_sensor[i].attitude[j] == recv_msg.imu_sensor[i].attitude[j]);
		}
	}
	for (int i = 0; i < 50; i++) {
		assert(test_msg.message[i] == recv_msg.message[i]);
	}
	assert(test_msg.timestamp == recv_msg.timestamp);

	printf("All tests passed.\n");

	return 0;
}

#endif // TEST_ENABLED