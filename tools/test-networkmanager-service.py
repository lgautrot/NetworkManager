#!/usr/bin/env python
# -*- Mode: python; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-

from __future__ import print_function

from gi.repository import GLib
import sys
import dbus
import dbus.service
import dbus.mainloop.glib
import random

mainloop = GLib.MainLoop()

# NM State
NM_STATE_UNKNOWN          = 0
NM_STATE_ASLEEP           = 10
NM_STATE_DISCONNECTED     = 20
NM_STATE_DISCONNECTING    = 30
NM_STATE_CONNECTING       = 40
NM_STATE_CONNECTED_LOCAL  = 50
NM_STATE_CONNECTED_SITE   = 60
NM_STATE_CONNECTED_GLOBAL = 70

# Device state
NM_DEVICE_STATE_UNKNOWN      = 0
NM_DEVICE_STATE_UNMANAGED    = 10
NM_DEVICE_STATE_UNAVAILABLE  = 20
NM_DEVICE_STATE_DISCONNECTED = 30
NM_DEVICE_STATE_PREPARE      = 40
NM_DEVICE_STATE_CONFIG       = 50
NM_DEVICE_STATE_NEED_AUTH    = 60
NM_DEVICE_STATE_IP_CONFIG    = 70
NM_DEVICE_STATE_IP_CHECK     = 80
NM_DEVICE_STATE_SECONDARIES  = 90
NM_DEVICE_STATE_ACTIVATED    = 100
NM_DEVICE_STATE_DEACTIVATING = 110
NM_DEVICE_STATE_FAILED       = 120

# Device type
NM_DEVICE_TYPE_UNKNOWN    = 0
NM_DEVICE_TYPE_ETHERNET   = 1
NM_DEVICE_TYPE_WIFI       = 2
NM_DEVICE_TYPE_UNUSED1    = 3
NM_DEVICE_TYPE_UNUSED2    = 4
NM_DEVICE_TYPE_BT         = 5
NM_DEVICE_TYPE_OLPC_MESH  = 6
NM_DEVICE_TYPE_WIMAX      = 7
NM_DEVICE_TYPE_MODEM      = 8
NM_DEVICE_TYPE_INFINIBAND = 9
NM_DEVICE_TYPE_BOND       = 10
NM_DEVICE_TYPE_VLAN       = 11
NM_DEVICE_TYPE_ADSL       = 12
NM_DEVICE_TYPE_BRIDGE     = 13
NM_DEVICE_TYPE_GENERIC    = 14
NM_DEVICE_TYPE_TEAM       = 15

# AC state
NM_ACTIVE_CONNECTION_STATE_UNKNOWN      = 0
NM_ACTIVE_CONNECTION_STATE_ACTIVATING   = 1
NM_ACTIVE_CONNECTION_STATE_ACTIVATED    = 2
NM_ACTIVE_CONNECTION_STATE_DEACTIVATING = 3
NM_ACTIVE_CONNECTION_STATE_DEACTIVATED  = 4

#########################################################
IFACE_DBUS = 'org.freedesktop.DBus'

class UnknownInterfaceException(dbus.DBusException):
    _dbus_error_name = IFACE_DBUS + '.UnknownInterface'

class UnknownPropertyException(dbus.DBusException):
    _dbus_error_name = IFACE_DBUS + '.UnknownProperty'

def to_path_array(src):
    array = dbus.Array([], signature=dbus.Signature('o'))
    for o in src:
        array.append(to_path(o))
    return array

def to_path(src):
    if src:
        return dbus.ObjectPath(src.path)
    return dbus.ObjectPath("/")

class ExportedObj(dbus.service.Object):
    def __init__(self, bus, object_path):
        dbus.service.Object.__init__(self, bus, object_path)
        self._bus = bus
        self.path = object_path
        self.__dbus_ifaces = {}

    def add_dbus_interface(self, dbus_iface, get_props_func):
        self.__dbus_ifaces[dbus_iface] = get_props_func

    def _get_dbus_properties(self, iface):
        return self.__dbus_ifaces[iface]()

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface not in self.__dbus_ifaces.keys():
            raise UnknownInterfaceException()
        return self._get_dbus_properties(iface)

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
    def Get(self, iface, name):
        if iface not in self.__dbus_ifaces.keys():
            raise UnknownInterfaceException()
        props = self._get_dbus_properties(iface)
        if not name in props.keys():
            raise UnknownPropertyException()
        return props[name]

###################################################################
IFACE_DEVICE = 'org.freedesktop.NetworkManager.Device'

class NotSoftwareException(dbus.DBusException):
    _dbus_error_name = IFACE_DEVICE + '.NotSoftware'

PD_UDI = "Udi"
PD_IFACE = "Interface"
PD_DRIVER = "Driver"
PD_STATE = "State"
PD_ACTIVE_CONNECTION = "ActiveConnection"
PD_IP4_CONFIG = "Ip4Config"
PD_IP6_CONFIG = "Ip6Config"
PD_DHCP4_CONFIG = "Dhcp4Config"
PD_DHCP6_CONFIG = "Dhcp6Config"
PD_MANAGED = "Managed"
PD_AUTOCONNECT = "Autoconnect"
PD_DEVICE_TYPE = "DeviceType"
PD_AVAILABLE_CONNECTIONS = "AvailableConnections"

class Device(ExportedObj):
    counter = 1

    def __init__(self, bus, iface, devtype):
        object_path = "/org/freedesktop/NetworkManager/Devices/%d" % Device.counter
        Device.counter = Device.counter + 1
        ExportedObj.__init__(self, bus, object_path)
        self.add_dbus_interface(IFACE_DEVICE, self.__get_props)

        self.iface = iface
        self.udi = "/sys/devices/virtual/%s" % iface
        self.devtype = devtype
        self.active_connection = None
        self.state = NM_DEVICE_STATE_UNAVAILABLE
        self.ip4_config = None
        self.ip6_config = None
        self.dhcp4_config = None
        self.dhcp6_config = None
        self.available_connections = []

    # Properties interface
    def __get_props(self):
        props = {}
        props[PD_UDI] = self.udi
        props[PD_IFACE] = self.iface
        props[PD_DRIVER] = "virtual"
        props[PD_STATE] = dbus.UInt32(self.state)
        props[PD_ACTIVE_CONNECTION] = to_path(self.active_connection)
        props[PD_IP4_CONFIG] = to_path(self.ip4_config)
        props[PD_IP6_CONFIG] = to_path(self.ip6_config)
        props[PD_DHCP4_CONFIG] = to_path(self.dhcp4_config)
        props[PD_DHCP6_CONFIG] = to_path(self.dhcp6_config)
        props[PD_MANAGED] = True
        props[PD_AUTOCONNECT] = True
        props[PD_DEVICE_TYPE] = dbus.UInt32(self.devtype)
        props[PD_AVAILABLE_CONNECTIONS] = to_path_array(self.available_connections)
        return props

    # methods
    @dbus.service.method(dbus_interface=IFACE_DEVICE, in_signature='', out_signature='')
    def Disconnect(self):
        pass

    @dbus.service.method(dbus_interface=IFACE_DEVICE, in_signature='', out_signature='')
    def Delete(self):
        # We don't currently support any software device types, so...
        raise NotSoftwareException()
        pass

    def __notify(self, propname):
        props = self._get_dbus_properties(IFACE_DEVICE)
        changed = { propname: props[propname] }
        Device.PropertiesChanged(self, changed)

    @dbus.service.signal(IFACE_DEVICE, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

    def set_active_connection(self, ac):
        self.active_connection = ac
        self.__notify(PD_ACTIVE_CONNECTION)

###################################################################

def random_mac():
    return '%02X:%02X:%02X:%02X:%02X:%02X' % (
        random.randint(0, 255), random.randint(0, 255), random.randint(0, 255),
        random.randint(0, 255), random.randint(0, 255), random.randint(0, 255)
      )

###################################################################
IFACE_WIRED = 'org.freedesktop.NetworkManager.Device.Wired'

PE_HW_ADDRESS = "HwAddress"
PE_PERM_HW_ADDRESS = "PermHwAddress"
PE_SPEED = "Speed"
PE_CARRIER = "Carrier"
PE_S390_SUBCHANNELS = "S390Subchannels"

class WiredDevice(Device):
    def __init__(self, bus, iface, mac, subchannels):
        Device.__init__(self, bus, iface, NM_DEVICE_TYPE_ETHERNET)
        self.add_dbus_interface(IFACE_WIRED, self.__get_props)

        if mac is None:
            self.mac = random_mac()
        else:
            self.mac = mac
        self.carrier = False
        self.s390_subchannels = subchannels

    # Properties interface
    def __get_props(self):
        props = {}
        props[PE_HW_ADDRESS] = self.mac
        props[PE_PERM_HW_ADDRESS] = self.mac
        props[PE_SPEED] = dbus.UInt32(100)
        props[PE_CARRIER] = self.carrier
        props[PE_S390_SUBCHANNELS] = self.s390_subchannels
        return props

    def __notify(self, propname):
        props = self._get_dbus_properties(IFACE_WIRED)
        changed = { propname: props[propname] }
        WiredDevice.PropertiesChanged(self, changed)

    @dbus.service.signal(IFACE_WIRED, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

###################################################################
IFACE_VLAN = 'org.freedesktop.NetworkManager.Device.Vlan'

PV_HW_ADDRESS = "HwAddress"
PV_CARRIER = "Carrier"
PV_VLAN_ID = "VlanId"

class VlanDevice(Device):
    def __init__(self, bus, iface):
        Device.__init__(self, bus, iface, NM_DEVICE_TYPE_VLAN)
        self.add_dbus_interface(IFACE_VLAN, self.__get_props)

        self.mac = random_mac()
        self.carrier = False
        self.vlan_id = 1

    # Properties interface
    def __get_props(self):
        props = {}
        props[PV_HW_ADDRESS] = self.mac
        props[PV_CARRIER] = self.carrier
        props[PV_VLAN_ID] = self.vlan_id
        return props

    @dbus.service.signal(IFACE_VLAN, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

###################################################################
IFACE_WIFI_AP = 'org.freedesktop.NetworkManager.AccessPoint'

PP_FLAGS = "Flags"
PP_WPA_FLAGS = "WpaFlags"
PP_RSN_FLAGS = "RsnFlags"
PP_SSID = "Ssid"
PP_FREQUENCY = "Frequency"
PP_HW_ADDRESS = "HwAddress"
PP_MODE = "Mode"
PP_MAX_BITRATE = "MaxBitrate"
PP_STRENGTH = "Strength"

class WifiAp(ExportedObj):
    counter = 0

    def __init__(self, bus, ssid, mac, flags, wpaf, rsnf, freq):
        path = "/org/freedesktop/NetworkManager/AccessPoint/%d" % WifiAp.counter
        WifiAp.counter = WifiAp.counter + 1
        ExportedObj.__init__(self, bus, path)
        self.add_dbus_interface(IFACE_WIFI_AP, self.__get_props)

        self.ssid = ssid
        if mac:
            self.bssid = mac
        else:
            self.bssid = random_mac()
        self.flags = flags
        self.wpaf = wpaf
        self.rsnf = rsnf
        self.freq = freq
        self.strength = random.randint(0, 100)
        self.strength_id = GLib.timeout_add_seconds(10, self.strength_cb, None)

    def __del__(self):
        if self.strength_id > 0:
            GLib.source_remove(self.strength_id)
        self.strength_id = 0

    def strength_cb(self, ignored):
        self.strength = random.randint(0, 100)
        self.__notify(PP_STRENGTH)
        return True

    # Properties interface
    def __get_props(self):
        props = {}
        props[PP_FLAGS] = dbus.UInt32(self.flags)
        props[PP_WPA_FLAGS] = dbus.UInt32(self.wpaf)
        props[PP_RSN_FLAGS] = dbus.UInt32(self.rsnf)
        props[PP_SSID] = dbus.ByteArray(self.ssid.encode('utf-8'))
        props[PP_FREQUENCY] = dbus.UInt32(self.freq)
        props[PP_HW_ADDRESS] = self.bssid
        props[PP_MODE] = dbus.UInt32(2)  # NM_802_11_MODE_INFRA
        props[PP_MAX_BITRATE] = dbus.UInt32(54000)
        props[PP_STRENGTH] = dbus.Byte(self.strength)
        return props

    def __notify(self, propname):
        props = self._get_dbus_properties(IFACE_WIFI_AP)
        changed = { propname: props[propname] }
        WifiAp.PropertiesChanged(self, changed)

    @dbus.service.signal(IFACE_WIFI_AP, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

###################################################################
IFACE_WIFI = 'org.freedesktop.NetworkManager.Device.Wireless'

class ApNotFoundException(dbus.DBusException):
    _dbus_error_name = IFACE_WIFI + '.AccessPointNotFound'

PW_HW_ADDRESS = "HwAddress"
PW_PERM_HW_ADDRESS = "PermHwAddress"
PW_MODE = "Mode"
PW_BITRATE = "Bitrate"
PW_ACCESS_POINTS = "AccessPoints"
PW_ACTIVE_ACCESS_POINT = "ActiveAccessPoint"
PW_WIRELESS_CAPABILITIES = "WirelessCapabilities"

class WifiDevice(Device):
    def __init__(self, bus, iface):
        Device.__init__(self, bus, iface, NM_DEVICE_TYPE_WIFI)
        self.add_dbus_interface(IFACE_WIFI, self.__get_props)

        self.mac = random_mac()
        self.aps = []
        self.active_ap = None

    # methods
    @dbus.service.method(dbus_interface=IFACE_WIFI, in_signature='', out_signature='ao')
    def GetAccessPoints(self):
        # only include non-hidden APs
        return to_path_array([a for a in self.aps if a.ssid])

    @dbus.service.method(dbus_interface=IFACE_WIFI, in_signature='', out_signature='ao')
    def GetAllAccessPoints(self):
        # include all APs including hidden ones
        return to_path_array(self.aps)

    @dbus.service.method(dbus_interface=IFACE_WIFI, in_signature='a{sv}', out_signature='')
    def RequestScan(self, props):
        pass

    @dbus.service.signal(IFACE_WIFI, signature='o')
    def AccessPointAdded(self, ap_path):
        pass

    def add_ap(self, ap):
        self.aps.append(ap)
        self.__notify(PW_ACCESS_POINTS)
        self.AccessPointAdded(to_path(ap))

    @dbus.service.signal(IFACE_WIFI, signature='o')
    def AccessPointRemoved(self, ap_path):
        pass

    def remove_ap(self, ap):
        self.aps.remove(ap)
        self.__notify(PW_ACCESS_POINTS)
        self.AccessPointRemoved(to_path(ap))

    # Properties interface
    def __get_props(self):
        props = {}
        props[PW_HW_ADDRESS] = self.mac
        props[PW_PERM_HW_ADDRESS] = self.mac
        props[PW_MODE] = dbus.UInt32(3)  # NM_802_11_MODE_INFRA
        props[PW_BITRATE] = dbus.UInt32(21000)
        props[PW_WIRELESS_CAPABILITIES] = dbus.UInt32(0xFF)
        props[PW_ACCESS_POINTS] = to_path_array(self.aps)
        props[PW_ACTIVE_ACCESS_POINT] = to_path(self.active_ap)
        return props

    def __notify(self, propname):
        props = self._get_dbus_properties(IFACE_WIFI)
        changed = { propname: props[propname] }
        WifiDevice.PropertiesChanged(self, changed)

    @dbus.service.signal(IFACE_WIFI, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

    # test functions
    def add_test_ap(self, ssid, mac):
        ap = WifiAp(self._bus, ssid, mac, 0x1, 0x1cc, 0x1cc, 2412)
        self.add_ap(ap)
        return ap

    def remove_ap_by_path(self, path):
        for ap in self.aps:
            if ap.path == path:
                self.remove_ap(ap)
                return
        raise ApNotFoundException("AP %s not found" % path)


###################################################################
IFACE_WIMAX_NSP = 'org.freedesktop.NetworkManager.WiMax.Nsp'

PN_NAME = "Name"
PN_SIGNAL_QUALITY = "SignalQuality"
PN_NETWORK_TYPE = "NetworkType"

class WimaxNsp(ExportedObj):
    counter = 0

    def __init__(self, bus, name):
        path = "/org/freedesktop/NetworkManager/Nsp/%d" % WimaxNsp.counter
        WimaxNsp.counter = WimaxNsp.counter + 1
        ExportedObj.__init__(self, bus, path)
        self.add_dbus_interface(IFACE_WIMAX_NSP, self.__get_props)

        self.name = name
        self.strength = random.randint(0, 100)
        self.strength_id = GLib.timeout_add_seconds(10, self.strength_cb, None)

    def __del__(self):
        if self.strength_id > 0:
            GLib.source_remove(self.strength_id)
        self.strength_id = 0

    def strength_cb(self, ignored):
        self.strength = random.randint(0, 100)
        self.__notify(PN_SIGNAL_QUALITY)
        return True

    # Properties interface
    def __get_props(self):
        props = {}
        props[PN_NAME] = self.name
        props[PN_SIGNAL_QUALITY] = dbus.UInt32(self.strength)
        props[PN_NETWORK_TYPE] = dbus.UInt32(0x1)  # NM_WIMAX_NSP_NETWORK_TYPE_HOME
        return props

    def __notify(self, propname):
        props = self._get_dbus_properties(IFACE_WIMAX_NSP)
        changed = { propname: props[propname] }
        WimaxNsp.PropertiesChanged(self, changed)

    @dbus.service.signal(IFACE_WIMAX_NSP, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

###################################################################
IFACE_WIMAX = 'org.freedesktop.NetworkManager.Device.WiMax'

class NspNotFoundException(dbus.DBusException):
    _dbus_error_name = IFACE_WIMAX + '.NspNotFound'

PX_NSPS = "Nsps"
PX_HW_ADDRESS = "HwAddress"
PX_CENTER_FREQUENCY = "CenterFrequency"
PX_RSSI = "Rssi"
PX_CINR = "Cinr"
PX_TX_POWER = "TxPower"
PX_BSID = "Bsid"
PX_ACTIVE_NSP = "ActiveNsp"

class WimaxDevice(Device):
    def __init__(self, bus, iface):
        Device.__init__(self, bus, iface, NM_DEVICE_TYPE_WIMAX)
        self.add_dbus_interface(IFACE_WIMAX, self.__get_props)

        self.mac = random_mac()
        self.bsid = random_mac()
        self.nsps = []
        self.active_nsp = None

    # methods
    @dbus.service.method(dbus_interface=IFACE_WIMAX, in_signature='', out_signature='ao')
    def GetNspList(self):
        # include all APs including hidden ones
        return to_path_array(self.nsps)

    @dbus.service.signal(IFACE_WIMAX, signature='o')
    def NspAdded(self, nsp_path):
        pass

    def add_nsp(self, nsp):
        self.nsps.append(nsp)
        self.__notify(PX_NSPS)
        self.NspAdded(to_path(nsp))

    @dbus.service.signal(IFACE_WIMAX, signature='o')
    def NspRemoved(self, nsp_path):
        pass

    def remove_nsp(self, nsp):
        self.nsps.remove(nsp)
        self.__notify(PX_NSPS)
        self.NspRemoved(to_path(nsp))

    # Properties interface
    def __get_props(self):
        props = {}
        props[PX_HW_ADDRESS] = self.mac
        props[PX_CENTER_FREQUENCY] = dbus.UInt32(2525)
        props[PX_RSSI] = dbus.Int32(-48)
        props[PX_CINR] = dbus.Int32(24)
        props[PX_TX_POWER] = dbus.Int32(9)
        props[PX_BSID] = self.bsid
        props[PX_NSPS] = to_path_array(self.nsps)
        props[PX_ACTIVE_NSP] = to_path(self.active_nsp)
        return props

    def __notify(self, propname):
        props = self._get_dbus_properties(IFACE_WIMAX)
        changed = { propname: props[propname] }
        WimaxDevice.PropertiesChanged(self, changed)

    @dbus.service.signal(IFACE_WIMAX, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

    # test functions
    def add_test_nsp(self, name):
        nsp = WimaxNsp(self._bus, name)
        self.add_nsp(nsp)
        return nsp

    def remove_nsp_by_path(self, path):
        for nsp in self.nsps:
            if nsp.path == path:
                self.remove_nsp(nsp)
                return
        raise NspNotFoundException("NSP %s not found" % path)

###################################################################
IFACE_ACTIVE_CONNECTION = 'org.freedesktop.NetworkManager.Connection.Active'

PAC_CONNECTION = "Connection"
PAC_SPECIFIC_OBJECT = "SpecificObject"
PAC_ID = "Id"
PAC_UUID = "Uuid"
PAC_TYPE = "Type"
PAC_DEVICES = "Devices"
PAC_STATE = "State"
PAC_DEFAULT = "Default"
PAC_IP4CONFIG = "Ip4Config"
PAC_DHCP4CONFIG = "Dhcp4Config"
PAC_DEFAULT6 = "Default6"
PAC_IP6CONFIG = "Ip6Config"
PAC_DHCP6CONFIG = "Dhcp6Config"
PAC_VPN = "Vpn"
PAC_MASTER = "Master"

class ActiveConnection(ExportedObj):
    counter = 1

    def __init__(self, bus, device, connection, specific_object):
        object_path = "/org/freedesktop/NetworkManager/ActiveConnection/%d" % ActiveConnection.counter
        ActiveConnection.counter = ActiveConnection.counter + 1
        ExportedObj.__init__(self, bus, object_path)
        self.add_dbus_interface(IFACE_ACTIVE_CONNECTION, self.__get_props)

        self.device = device
        self.conn = connection
        self.specific_object = specific_object
        self.state = NM_ACTIVE_CONNECTION_STATE_UNKNOWN
        self.default = False
        self.ip4config = None
        self.dhcp4config = None
        self.default6 = False
        self.ip6config = None
        self.dhcp6config = None
        self.vpn = False
        self.master = None

    # Properties interface
    def __get_props(self):
        props = {}
        props[PAC_CONNECTION] = to_path(self.conn)
        props[PAC_SPECIFIC_OBJECT] = to_path(self.specific_object)
        conn_settings = self.conn.GetSettings()
        s_con = conn_settings['connection']
        props[PAC_ID] = s_con['id']
        props[PAC_UUID] = s_con['uuid']
        props[PAC_TYPE] = s_con['type']
        props[PAC_DEVICES] = to_path_array([self.device])
        props[PAC_STATE] = dbus.UInt32(self.state)
        props[PAC_DEFAULT] = self.default
        props[PAC_IP4CONFIG] = to_path(self.ip4config)
        props[PAC_DHCP4CONFIG] = to_path(self.dhcp4config)
        props[PAC_DEFAULT6] = self.default6
        props[PAC_IP6CONFIG] = to_path(self.ip6config)
        props[PAC_DHCP6CONFIG] = to_path(self.dhcp6config)
        props[PAC_VPN] = self.vpn
        props[PAC_MASTER] = to_path(self.master)
        return props

    @dbus.service.signal(IFACE_ACTIVE_CONNECTION, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

###################################################################
IFACE_TEST = 'org.freedesktop.NetworkManager.LibnmGlibTest'
IFACE_NM = 'org.freedesktop.NetworkManager'

class PermissionDeniedException(dbus.DBusException):
    _dbus_error_name = IFACE_NM + '.PermissionDenied'

class UnknownDeviceException(dbus.DBusException):
    _dbus_error_name = IFACE_NM + '.UnknownDevice'

class UnknownConnectionException(dbus.DBusException):
    _dbus_error_name = IFACE_NM + '.UnknownConnection'

PM_DEVICES = 'Devices'
PM_NETWORKING_ENABLED = 'NetworkingEnabled'
PM_WWAN_ENABLED = 'WwanEnabled'
PM_WWAN_HARDWARE_ENABLED = 'WwanHardwareEnabled'
PM_WIRELESS_ENABLED = 'WirelessEnabled'
PM_WIRELESS_HARDWARE_ENABLED = 'WirelessHardwareEnabled'
PM_WIMAX_ENABLED = 'WimaxEnabled'
PM_WIMAX_HARDWARE_ENABLED = 'WimaxHardwareEnabled'
PM_ACTIVE_CONNECTIONS = 'ActiveConnections'
PM_PRIMARY_CONNECTION = 'PrimaryConnection'
PM_ACTIVATING_CONNECTION = 'ActivatingConnection'
PM_STARTUP = 'Startup'
PM_STATE = 'State'
PM_VERSION = 'Version'
PM_CONNECTIVITY = 'Connectivity'

def set_device_ac_cb(device, ac):
    device.set_active_connection(ac)

class NetworkManager(ExportedObj):
    def __init__(self, bus, object_path):
        ExportedObj.__init__(self, bus, object_path)
        self.add_dbus_interface(IFACE_NM, self.__get_props)

        self._bus = bus;
        self.devices = []
        self.active_connections = []
        self.primary_connection = None
        self.activating_connection = None
        self.state = NM_STATE_DISCONNECTED
        self.connectivity = 1

    @dbus.service.signal(IFACE_NM, signature='u')
    def StateChanged(self, new_state):
        pass

    def set_state(self, new_state):
        self.state = new_state
        self.__notify(PM_STATE)
        self.StateChanged(dbus.UInt32(self.state))

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='', out_signature='ao')
    def GetDevices(self):
        return self._get_dbus_properties(IFACE_NM)[PM_DEVICES]

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='s', out_signature='o')
    def GetDeviceByIpIface(self, ip_iface):
        for d in self.devices:
            # ignore iface/ip_iface distinction for now
            if d.iface == ip_iface:
                return to_path(d)
        raise UnknownDeviceException("No device found for the requested iface.")

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='ooo', out_signature='o')
    def ActivateConnection(self, conpath, devpath, specific_object):
        try:
            connection = settings.get_connection(conpath)
        except Exception as e:
            raise UnknownConnectionException("Connection not found")

        hash = connection.GetSettings()
        s_con = hash['connection']

        device = None
        for d in self.devices:
            if d.path == devpath:
                device = d
                break
        if not device and s_con['type'] == 'vlan':
            ifname = s_con['interface-name']
            device = VlanDevice(self._bus, ifname)
            self.add_device(device)
        if not device:
            raise UnknownDeviceException("No device found for the requested iface.")

        # See if we need secrets. For the moment, we only support WPA
        if '802-11-wireless-security' in hash:
            s_wsec = hash['802-11-wireless-security']
            if (s_wsec['key-mgmt'] == 'wpa-psk' and 'psk' not in s_wsec):
                secrets = agent_manager.get_secrets(hash, conpath, '802-11-wireless-security')
                if secrets is None:
                    raise NoSecretsException("No secret agent available")
                if '802-11-wireless-security' not in secrets:
                    raise NoSecretsException("No secrets provided")
                s_wsec = secrets['802-11-wireless-security']
                if 'psk' not in s_wsec:
                    raise NoSecretsException("No secrets provided")

        ac = ActiveConnection(self._bus, device, connection, None)
        self.active_connections.append(ac)
        self.__notify(PM_ACTIVE_CONNECTIONS)

        if s_con['id'] == 'object-creation-failed-test':
            self.active_connections.remove(ac)
            ac.remove_from_connection()
        else:
            GLib.timeout_add(50, set_device_ac_cb, device, ac)

        return to_path(ac)

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='a{sa{sv}}oo', out_signature='oo')
    def AddAndActivateConnection(self, connection, devpath, specific_object):
        device = None
        for d in self.devices:
            if d.path == devpath:
                device = d
                break
        if not device:
            raise UnknownDeviceException("No device found for the requested iface.")

        conpath = settings.AddConnection(connection)
        return (conpath, self.ActivateConnection(conpath, devpath, specific_object))

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='o', out_signature='')
    def DeactivateConnection(self, active_connection):
        pass

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='b', out_signature='')
    def Sleep(self, do_sleep):
        if do_sleep:
            self.state = NM_STATE_ASLEEP
        else:
            self.state = NM_STATE_DISCONNECTED
        self.__notify(PM_STATE)

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='b', out_signature='')
    def Enable(self, do_enable):
        pass

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='', out_signature='a{ss}')
    def GetPermissions(self):
        return { "org.freedesktop.NetworkManager.enable-disable-network":   "yes",
                 "org.freedesktop.NetworkManager.sleep-wake":               "no",
                 "org.freedesktop.NetworkManager.enable-disable-wifi":      "yes",
                 "org.freedesktop.NetworkManager.enable-disable-wwan":      "yes",
                 "org.freedesktop.NetworkManager.enable-disable-wimax":     "yes",
                 "org.freedesktop.NetworkManager.network-control":          "yes",
                 "org.freedesktop.NetworkManager.wifi.share.protected":     "yes",
                 "org.freedesktop.NetworkManager.wifi.share.open":          "yes",
                 "org.freedesktop.NetworkManager.settings.modify.own":      "yes",
                 "org.freedesktop.NetworkManager.settings.modify.system":   "yes",
                 "org.freedesktop.NetworkManager.settings.modify.hostname": "yes" }

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='ss', out_signature='')
    def SetLogging(self, level, domains):
        pass

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='', out_signature='ss')
    def GetLogging(self):
        return ("info", "HW,RFKILL,CORE,DEVICE,WIFI,ETHER")

    @dbus.service.method(dbus_interface=IFACE_NM, in_signature='', out_signature='u')
    def CheckConnectivity(self):
        raise PermissionDeniedException("You fail")

    @dbus.service.signal(IFACE_NM, signature='o')
    def DeviceAdded(self, devpath):
        pass

    def add_device(self, device):
        self.devices.append(device)
        self.__notify(PM_DEVICES)
        self.DeviceAdded(to_path(device))

    @dbus.service.signal(IFACE_NM, signature='o')
    def DeviceRemoved(self, devpath):
        pass

    def remove_device(self, device):
        self.devices.remove(device)
        self.__notify(PM_DEVICES)
        self.DeviceRemoved(to_path(device))

    ################# D-Bus Properties interface
    def __get_props(self):
        props = {}
        props[PM_DEVICES] = to_path_array(self.devices)
        props[PM_NETWORKING_ENABLED] = True
        props[PM_WWAN_ENABLED] = True
        props[PM_WWAN_HARDWARE_ENABLED] = True
        props[PM_WIRELESS_ENABLED] = True
        props[PM_WIRELESS_HARDWARE_ENABLED] = True
        props[PM_WIMAX_ENABLED] = True
        props[PM_WIMAX_HARDWARE_ENABLED] = True
        props[PM_ACTIVE_CONNECTIONS] = to_path_array(self.active_connections)
        props[PM_PRIMARY_CONNECTION] = to_path(self.primary_connection)
        props[PM_ACTIVATING_CONNECTION] = to_path(self.activating_connection)
        props[PM_STARTUP] = False
        props[PM_STATE] = dbus.UInt32(self.state)
        props[PM_VERSION] = "0.9.9.0"
        props[PM_CONNECTIVITY] = dbus.UInt32(self.connectivity)
        return props

    def __notify(self, propname):
        props = self._get_dbus_properties(IFACE_NM)
        changed = { propname: props[propname] }
        NetworkManager.PropertiesChanged(self, changed)

    @dbus.service.signal(IFACE_NM, signature='a{sv}')
    def PropertiesChanged(self, changed):
        pass

    ################# Testing methods
    @dbus.service.method(IFACE_TEST, in_signature='', out_signature='')
    def Quit(self):
        mainloop.quit()

    @dbus.service.method(IFACE_TEST, in_signature='ssas', out_signature='o')
    def AddWiredDevice(self, ifname, mac, subchannels):
        for d in self.devices:
            if d.iface == ifname:
                raise PermissionDeniedException("Device already added")
        dev = WiredDevice(self._bus, ifname, mac, subchannels)
        self.add_device(dev)
        return to_path(dev)

    @dbus.service.method(IFACE_TEST, in_signature='s', out_signature='o')
    def AddWifiDevice(self, ifname):
        for d in self.devices:
            if d.iface == ifname:
                raise PermissionDeniedException("Device already added")
        dev = WifiDevice(self._bus, ifname)
        self.add_device(dev)
        return to_path(dev)

    @dbus.service.method(IFACE_TEST, in_signature='s', out_signature='o')
    def AddWimaxDevice(self, ifname):
        for d in self.devices:
            if d.iface == ifname:
                raise PermissionDeniedException("Device already added")
        dev = WimaxDevice(self._bus, ifname)
        self.add_device(dev)
        return to_path(dev)

    @dbus.service.method(IFACE_TEST, in_signature='o', out_signature='')
    def RemoveDevice(self, path):
        for d in self.devices:
            if d.path == path:
                self.remove_device(d)
                return
        raise UnknownDeviceException("Device not found")

    @dbus.service.method(IFACE_TEST, in_signature='sss', out_signature='o')
    def AddWifiAp(self, ifname, ssid, mac):
        for d in self.devices:
            if d.iface == ifname:
                return to_path(d.add_test_ap(ssid, mac))
        raise UnknownDeviceException("Device not found")

    @dbus.service.method(IFACE_TEST, in_signature='so', out_signature='')
    def RemoveWifiAp(self, ifname, ap_path):
        for d in self.devices:
            if d.iface == ifname:
                d.remove_ap_by_path(ap_path)
                return
        raise UnknownDeviceException("Device not found")

    @dbus.service.method(IFACE_TEST, in_signature='ss', out_signature='o')
    def AddWimaxNsp(self, ifname, name):
        for d in self.devices:
            if d.iface == ifname:
                return to_path(d.add_test_nsp(name))
        raise UnknownDeviceException("Device not found")

    @dbus.service.method(IFACE_TEST, in_signature='so', out_signature='')
    def RemoveWimaxNsp(self, ifname, nsp_path):
        for d in self.devices:
            if d.iface == ifname:
                d.remove_nsp_by_path(nsp_path)
                return
        raise UnknownDeviceException("Device not found")

    @dbus.service.method(IFACE_TEST, in_signature='', out_signature='')
    def AutoRemoveNextConnection(self):
        settings.auto_remove_next_connection()

###################################################################
IFACE_CONNECTION = 'org.freedesktop.NetworkManager.Settings.Connection'

class InvalidPropertyException(dbus.DBusException):
    _dbus_error_name = IFACE_CONNECTION + '.InvalidProperty'

class MissingPropertyException(dbus.DBusException):
    _dbus_error_name = IFACE_CONNECTION + '.MissingProperty'

class InvalidSettingException(dbus.DBusException):
    _dbus_error_name = IFACE_CONNECTION + '.InvalidSetting'

class MissingSettingException(dbus.DBusException):
    _dbus_error_name = IFACE_CONNECTION + '.MissingSetting'

class Connection(dbus.service.Object):
    def __init__(self, bus, object_path, settings, remove_func):
        dbus.service.Object.__init__(self, bus, object_path)

        if 'connection' not in settings:
            raise MissingSettingException('connection: setting is required')
        s_con = settings['connection']
        if 'type' not in s_con:
            raise MissingPropertyException('connection.type: property is required')
        type = s_con['type']
        if not type in ['802-3-ethernet', '802-11-wireless', 'vlan', 'wimax']:
            raise InvalidPropertyException('connection.type: unsupported connection type')

        self.path = object_path
        self.settings = settings
        self.remove_func = remove_func
        self.visible = True
        self.props = {}
        self.props['Unsaved'] = False

    # Properties interface
    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface != IFACE_CONNECTION:
            raise UnknownInterfaceException()
        return self.props

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
    def Get(self, iface, name):
        if iface != IFACE_CONNECTION:
            raise UnknownInterfaceException()
        if not name in self.props.keys():
            raise UnknownPropertyException()
        return self.props[name]

    # Connection methods
    @dbus.service.method(dbus_interface=IFACE_CONNECTION, in_signature='', out_signature='a{sa{sv}}')
    def GetSettings(self):
        if not self.visible:
            raise PermissionDeniedException()
        return self.settings

    @dbus.service.method(dbus_interface=IFACE_CONNECTION, in_signature='b', out_signature='')
    def SetVisible(self, vis):
        self.visible = vis
        self.Updated()

    @dbus.service.method(dbus_interface=IFACE_CONNECTION, in_signature='', out_signature='')
    def Delete(self):
        self.remove_func(self)
        self.Removed()
        self.remove_from_connection()

    @dbus.service.signal(IFACE_CONNECTION, signature='')
    def Removed(self):
        pass

    @dbus.service.signal(IFACE_CONNECTION, signature='')
    def Updated(self):
        pass

###################################################################
IFACE_SETTINGS = 'org.freedesktop.NetworkManager.Settings'

class InvalidHostnameException(dbus.DBusException):
    _dbus_error_name = IFACE_SETTINGS + '.InvalidHostname'

class Settings(dbus.service.Object):
    def __init__(self, bus, object_path):
        dbus.service.Object.__init__(self, bus, object_path)
        self.connections = {}
        self.bus = bus
        self.counter = 1
        self.remove_next_connection = False
        self.props = {}
        self.props['Hostname'] = "foobar.baz"
        self.props['CanModify'] = True
        self.props['Connections'] = dbus.Array([], 'o')

    def auto_remove_next_connection(self):
        self.remove_next_connection = True;

    def get_connection(self, path):
        return self.connections[path]

    @dbus.service.method(dbus_interface=IFACE_SETTINGS, in_signature='', out_signature='ao')
    def ListConnections(self):
        return self.connections.keys()

    @dbus.service.method(dbus_interface=IFACE_SETTINGS, in_signature='a{sa{sv}}', out_signature='o')
    def AddConnection(self, settings):
        path = "/org/freedesktop/NetworkManager/Settings/Connection/{0}".format(self.counter)
        self.counter = self.counter + 1
        self.connections[path] = Connection(self.bus, path, settings, self.delete_connection)
        self.props['Connections'] = dbus.Array(self.connections.keys(), 'o')
        self.NewConnection(path)
        self.PropertiesChanged({ 'connections': self.props['Connections'] })

        if self.remove_next_connection:
            self.remove_next_connection = False
            self.connections[path].Delete()

        return path

    def delete_connection(self, connection):
        del self.connections[connection.path]
        self.props['Connections'] = dbus.Array(self.connections.keys(), 'o')
        self.PropertiesChanged({ 'connections': self.props['Connections'] })

    @dbus.service.method(dbus_interface=IFACE_SETTINGS, in_signature='s', out_signature='')
    def SaveHostname(self, hostname):
        # Arbitrary requirement to test error handling
        if hostname.find('.') == -1:
            raise InvalidHostnameException()
        self.props['Hostname'] = hostname
        self.PropertiesChanged({ 'hostname': hostname })

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        if iface != IFACE_SETTINGS:
            raise UnknownInterfaceException()
        return self.props

    @dbus.service.method(dbus_interface=dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
    def Get(self, iface, name):
        if iface != IFACE_SETTINGS:
            raise UnknownInterfaceException()
        if not name in self.props.keys():
            raise UnknownPropertyException()
        return self.props[name]

    @dbus.service.signal(IFACE_SETTINGS, signature='o')
    def NewConnection(self, path):
        pass

    @dbus.service.signal(IFACE_SETTINGS, signature='a{sv}')
    def PropertiesChanged(self, path):
        pass

    @dbus.service.method(IFACE_SETTINGS, in_signature='', out_signature='')
    def Quit(self):
        mainloop.quit()

###################################################################
IFACE_AGENT_MANAGER = 'org.freedesktop.NetworkManager.AgentManager'
IFACE_AGENT = 'org.freedesktop.NetworkManager.SecretAgent'

PATH_SECRET_AGENT = '/org/freedesktop/NetworkManager/SecretAgent'

FLAG_ALLOW_INTERACTION = 0x1
FLAG_REQUEST_NEW = 0x2
FLAG_USER_REQUESTED = 0x4

class NoSecretsException(dbus.DBusException):
    _dbus_error_name = IFACE_AGENT_MANAGER + '.NoSecrets'

class UserCanceledException(dbus.DBusException):
    _dbus_error_name = IFACE_AGENT_MANAGER + '.UserCanceled'

class AgentManager(dbus.service.Object):
    def __init__(self, bus, object_path):
        dbus.service.Object.__init__(self, bus, object_path)
        self.agents = {}
        self.bus = bus

    @dbus.service.method(dbus_interface=IFACE_AGENT_MANAGER,
                         in_signature='s', out_signature='',
                         sender_keyword='sender')
    def Register(self, name, sender=None):
        self.RegisterWithCapabilities(name, 0, sender)

    @dbus.service.method(dbus_interface=IFACE_AGENT_MANAGER,
                         in_signature='su', out_signature='',
                         sender_keyword='sender')
    def RegisterWithCapabilities(self, name, caps, sender=None):
        self.agents[sender] = self.bus.get_object(sender, PATH_SECRET_AGENT)

    @dbus.service.method(dbus_interface=IFACE_AGENT_MANAGER,
                         in_signature='', out_signature='',
                         sender_keyword='sender')
    def Unregister(self, sender=None):
        del self.agents[sender]

    def get_secrets(self, connection, path, setting_name):
        if len(self.agents) == 0:
            return None

        secrets = {}
        for sender in self.agents:
            agent = self.agents[sender]
            try:
                secrets = agent.GetSecrets(connection, path, setting_name,
                                           dbus.Array([], 's'),
                                           FLAG_ALLOW_INTERACTION | FLAG_USER_REQUESTED,
                                           dbus_interface=IFACE_AGENT)
                break
            except dbus.DBusException as e:
                if e.get_dbus_name() == IFACE_AGENT + '.UserCanceled':
                    raise UserCanceledException('User canceled')
                continue
        return secrets

###################################################################

def stdin_cb(io, condition):
    mainloop.quit()

def quit_cb(user_data):
    mainloop.quit()

def main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

    random.seed()

    bus = dbus.SessionBus()

    global manager, settings, agent_manager
    manager = NetworkManager(bus, "/org/freedesktop/NetworkManager")
    settings = Settings(bus, "/org/freedesktop/NetworkManager/Settings")
    agent_manager = AgentManager(bus, "/org/freedesktop/NetworkManager/AgentManager")

    if not bus.request_name("org.freedesktop.NetworkManager"):
        sys.exit(1)

    # Watch stdin; if it closes, assume our parent has crashed, and exit
    io = GLib.IOChannel(0)
    io.add_watch(GLib.IOCondition.HUP, stdin_cb)

    # also quit after inactivity to ensure we don't stick around if the above fails somehow
    GLib.timeout_add_seconds(20, quit_cb, None)

    try:
        mainloop.run()
    except Exception as e:
        pass

    sys.exit(0)

if __name__ == '__main__':
    main()

