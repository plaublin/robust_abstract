import java.net.*;
import java.io.*;
import java.util.*;
import java.sql.*;

public class PBFTConnection {
	// The default port.
	// /////////////////////////
	public static final int DEFAULT_PBFT_CLIENT_PORT = 9966;
	public static final int DEFAULT_PBFT_JAVA_PORT = 9967;

	// The maximum length of a message.
	// ////////////////////////////////////////
	private static final int MAX_MESSAGE_LENGTH = 9000;

	// The length of a header.
	// ////////////////////////////////////////
	private static final int HEADER_LENGTH = 40;
	private static int F = 1; // just default
	private static int AUTH_LENGTH = (3*F+1)*8+8; // (3f+1)*8+8
	public static int MAX_PAYLOAD_LENGTH 
		= MAX_MESSAGE_LENGTH - HEADER_LENGTH - AUTH_LENGTH;
	// Used to determine endianness.
	// //////////////////////////////
	private static final int ENDIAN_TYPE = 0x00000091;

	// The daemon's address.
	// //////////////////////
	private InetAddress address;

	// The daemon's port.
	// ///////////////////
	private int port;
	
	// Keep stats of all request ids
	/////////////////////////////////
	private HashMap<Integer, Long> rids;

	// The socket this connection is using.
	// /////////////////////////////////////
	private DatagramSocket socket;

	// Used for storing splitted messages
	// ////////////////////////////////////
	private int maxClientId;
	private List<byte[]>[] receivedMessages;

	// Checks if the int is the same-endian type as the local machine.
	// ////////////////////////////////////////////////////////////////
	private static boolean sameEndian(int i) {
		return ((i & ENDIAN_TYPE) == 0);
	}

	// Clears the int's endian type.
	// //////////////////////////////
	private static int clearEndian(int i) {
		return (i & ~ENDIAN_TYPE);
	}

	// Endian-flips the int.
	// //////////////////////
	protected static int flip(int i) {
		return (((i >> 24) & 0x000000ff) | ((i >> 8) & 0x0000ff00)
				| ((i << 8) & 0x00ff0000) | ((i << 24) & 0xff000000));
	}

	// Endian-flips the short.
	// ////////////////////////
	private static short flip(short s) {
		return ((short) (((s >> 8) & 0x00ff) | ((s << 8) & 0xff00)));
	}

	// Puts a text into an array of bytes.
	// //////////////////////////////////////////
	private static int toBytes(String data, byte buffer[], int bufferIndex) {
		// Get the group's name.
		// //////////////////////
		byte name[];
		try {
			name = data.getBytes("ISO8859_1");
		} catch (UnsupportedEncodingException e) {
			// Already checked for this exception in connect.
			// ///////////////////////////////////////////////
			name = new byte[0];
		}
		// Copy the name into the buffer.
		// ///////////////////////////////
		int len = name.length;
		if (name.length > MAX_MESSAGE_LENGTH)
			len = MAX_MESSAGE_LENGTH;
		try {
			System.arraycopy(name, 0, buffer, bufferIndex, len);
		} catch (IndexOutOfBoundsException e) {
			System.out.println("toBytes failed, buffer length " + buffer.length
					+ ", index " + bufferIndex + "\n" + e);
			return 0;
		}
		return len;
	}

	// Puts a long into an array of bytes.
	// ////////////////////////////////////
	private static int toBytes(long i, byte buffer[], int bufferIndex) {
		buffer[bufferIndex++] = (byte) ((i) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 8) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 16) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 24) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 32) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 40) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 48) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 56) & 0xFF);
		return 8;
	}

	// Puts an int into an array of bytes.
	// ////////////////////////////////////
	private static int toBytes(int i, byte buffer[], int bufferIndex) {
		buffer[bufferIndex++] = (byte) ((i) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 8) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 16) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 24) & 0xFF);
		return 4;
	}

	// Puts a short into an array of bytes.
	// ////////////////////////////////////
	private static int toBytes(short i, byte buffer[], int bufferIndex) {
		buffer[bufferIndex++] = (byte) ((i) & 0xFF);
		buffer[bufferIndex++] = (byte) ((i >> 8) & 0xFF);
		return 2;
	}

	// Gets an int from an array of bytes.
	// ////////////////////////////////////
	protected static int toInt(byte buffer[], int bufferIndex) {
		int i0 = (buffer[bufferIndex++] & 0xFF);
		int i1 = (buffer[bufferIndex++] & 0xFF);
		int i2 = (buffer[bufferIndex++] & 0xFF);
		int i3 = (buffer[bufferIndex++] & 0xFF);

		return ((i3 << 24) | (i2 << 16) | (i1 << 8) | (i0));
	}

	// Reads from inputsocket until all bytes read or exception raised
	// ////////////////////////////////////////////////////////////////
	private void readBytesFromSocket(byte buffer[]) throws Exception {
		int byteIndex;
		int rcode;
		DatagramPacket recvp = new DatagramPacket(buffer, buffer.length);

		try {
			socket.receive(recvp);
		} catch (InterruptedIOException e) {
			throw new Exception(
					"readBytesFromSocket(): InterruptedIOException " + e);
		} catch (IOException e) {
			throw new Exception("readBytesFromSocket(): read() " + e);
		}

	}

	// Gets a string from an array of bytes.
	// //////////////////////////////////////
	protected String extractString(byte buffer[], int bufferIndex) {
		try {
			for (int end = bufferIndex; end < buffer.length; end++) {
				if (buffer[end] == 0) {
					// Get the group name.
					// ////////////////////
					String name = new String(buffer, bufferIndex, end
							- bufferIndex, "ISO8859_1");

					return name;
				}
			}
		} catch (UnsupportedEncodingException e) {
			// Already checked for this exception in connect.
			// ///////////////////////////////////////////////
		}

		return null;
	}

	// Constructor.
	// /////////////
	public PBFTConnection(int maxCid) {
		this(maxCid, 1);
	}

	public PBFTConnection(int maxCid, int f) {
		this.maxClientId = maxCid;
		F = f;
		AUTH_LENGTH = (3*F+1)*8+8;
		MAX_PAYLOAD_LENGTH = MAX_MESSAGE_LENGTH - HEADER_LENGTH - AUTH_LENGTH;

		receivedMessages = new List[maxClientId];
		rids = new HashMap<Integer, Long>();
		for (int i = 0; i < receivedMessages.length; i++) {
			receivedMessages[i] = new LinkedList<byte[]>();
			receivedMessages[i] = Collections
					.synchronizedList(receivedMessages[i]);

			rids.put(i, 1L);
		}
	}

	// Establishes a connection with the pbft daemon.
	// /////////////////////////////////////////////////
	/**
	 * Establishes a connection to a spread daemon. Groups can be joined, and
	 * messages can be sent or received once the connection has been
	 * established.
	 * 
	 * @param address
	 *            the daemon's address, or null to connect to the localhost
	 * @param port
	 *            the daemon's port, or 0 for the default port (9966)
	 * @param priority
	 *            if true, this is a priority connection
	 * @throws Exception
	 *             if the connection cannot be established
	 * @see Connection#disconnect()
	 */
	synchronized public void connect(InetAddress address, int port) throws Exception {
		// Is ISO8859_1 encoding supported?
		// /////////////////////////////
		try {
			new String("ASCII/ISO8859_1 encoding test").getBytes("ISO8859_1");
		} catch (UnsupportedEncodingException e) {
			throw new Exception("ISO8859_1 encoding is not supported.");
		}

		// Store member variables.
		// ////////////////////////
		this.address = address;
		this.port = port;
		
		// Check if no address was specified.
		// ///////////////////////////////////
		if (address == null) {
			// Use the local host.
			// ////////////////////
			try {
				address = InetAddress.getLocalHost();
				System.out.println("Address is now " + address);
			} catch (UnknownHostException e) {
				throw new Exception("Error getting local host");
			}
		}

		// Check if no port was specified.
		// ////////////////////////////////
		if (port == 0) {
			// Use the default port.
			// //////////////////////
			port = DEFAULT_PBFT_CLIENT_PORT;
		}

		// Check if the port is out of range.
		// ///////////////////////////////////
		if ((port < 0) || (port > (32 * 1024))) {
			throw new Exception("Bad pbft port (" + port + ").");
		}

		// Create the socket.
		// ///////////////////
		try {
			socket = new DatagramSocket();
			socket.setSoTimeout(0); // infinite timeout
			socket.setReceiveBufferSize(MAX_MESSAGE_LENGTH);
			socket.connect(address, port);
			System.out.println("PBFTConnection: Connected to "+address+" : port "+port);
		} catch (IOException e) {
			System.out.println("PBFTConnection: Could'nt connect to "+address+" : port "+port);
			throw new Exception("DatagramSocket(): " + e);
		}
	}
	
	synchronized public void setSocket(InetAddress addr, int port, DatagramSocket socket)
	{
		this.address = addr;
		this.port = port;
		this.socket = socket;
	}
	
	public static void prettyPrintHex(byte[] data) {
		int i = 0, j = 0; // loop counters
		int line_addr = 0; // memmory address printed on the left
		
		if (data.length == 0) {
			return;
		}

		StringBuilder _sbbuffer = new StringBuilder();

		// Loop through every input byte
		String _hexline = "";
		String _asciiline = "";
		for (i = 0, line_addr = 0; i < data.length; i++, line_addr++) {
			// Print the line numbers at the beginning of the line
			if ((i % 16) == 0) {
				if (i != 0) {
					_sbbuffer.append(_hexline);
					_sbbuffer.append("\t...\t");
					_sbbuffer.append(_asciiline + "\n");
				}
				_asciiline = "";
				_hexline = String.format("%#06x ", line_addr);
			}

			_hexline = _hexline.concat(String.format("%#04x ", data[i]));
			if (data[i] > 31 && data[i] < 127) {
				_asciiline = _asciiline.concat(String.valueOf((char) data[i]));
			} else {
				_asciiline = _asciiline.concat(".");
			}
		}
		// Handle the ascii for the final line, which may not be completely
		// filled.
		if (i % 16 > 0) {
			for (j = 0; j < 16 - (i % 16); j++) {
				_hexline = _hexline.concat("     ");
			}
			_sbbuffer.append(_hexline);
			_sbbuffer.append("\t...\t");
			_sbbuffer.append(_asciiline);
		}
		System.out.println(_sbbuffer.toString());
	}

	// Disconnects from the spread daemon.
	// ////////////////////////////////////
	/**
	 * Disconnects the connection to the daemon. Nothing else should be done
	 * with this connection after disconnecting it.
	 * 
	 * @throws Exception
	 *             if there is no connection or there is an error disconnecting
	 * @see Connection#connect(InetAddress, int, String, boolean, boolean)
	 */
	synchronized public void disconnect() throws Exception {
		// Close the socket.
		// //////////////////
		socket.close();
	}

	// Send
	// /////////////////////////////////
	// Sends the message.
	// ///////////////////
	/**
	 * Multicasts a message. The message will be sent to all the groups
	 * specified in the message.
	 * 
	 * @param message
	 *            the message to multicast
	 * @throws Exception
	 *             if there is no connection or if there is any error sending
	 *             the message
	 */
	private void send(int cid, byte[] message) throws Exception {
		// Calculate the total number of bytes.
		// /////////////////////////////////////
		int numBytes = HEADER_LENGTH; // serviceType, numGroups, type/hint,
		// dataLen
		int messageLength = message.length;
		int bytesInMessage = ((messageLength / 8) + 1) * 8;
		System.out.println("Message length is " + messageLength + "/"
				+ bytesInMessage);

		// Allocate the send buffer.
		// //////////////////////////
		byte buffer[] = new byte[MAX_MESSAGE_LENGTH];
		int bufferIndex = 0;

		// tag
		bufferIndex += toBytes((short) 1, buffer, bufferIndex);
		// extra
		bufferIndex += toBytes((short) 0, buffer, bufferIndex);
		// size
		bufferIndex += toBytes((int) (bytesInMessage + numBytes + 40), buffer,
				bufferIndex);
		// Digest
		// MD5(cid,rid,command==0);
		bufferIndex += toBytes((int) 0, buffer, bufferIndex);
		bufferIndex += toBytes((int) 0, buffer, bufferIndex);
		bufferIndex += toBytes((int) 0, buffer, bufferIndex);
		bufferIndex += toBytes((int) 0, buffer, bufferIndex);
		// replier
		bufferIndex += toBytes((short) 0, buffer, bufferIndex);
		// command_size
		bufferIndex += toBytes((short) message.length, buffer, bufferIndex);

		// cid
		bufferIndex += toBytes((int) cid, buffer, bufferIndex);
		// rid
		// bufferIndex += toBytes((long)1337, buffer, bufferIndex);
		// bufferIndex += toBytes((long) (System.currentTimeMillis()), buffer,
		//		bufferIndex);
                bufferIndex += toBytes((long)rids.get(cid), buffer, bufferIndex);
                rids.put(cid, rids.get(cid)+1);
		System.out.println("Header ends at " + bufferIndex);
		// The message data.
		// //////////////////
		try {
			System.arraycopy(message, 0, buffer, bufferIndex, message.length);
			bufferIndex += message.length;
		} catch (IndexOutOfBoundsException e) {
			throw new Exception("Message too big");
		}
		// authenticator
		// (3*f+1)*8+8 bytes
		int ndx = 0;
		for (; ndx < bytesInMessage - messageLength; ndx++) {
			buffer[bufferIndex + ndx] = 0;
		}
		bufferIndex += ndx;
		byte[] authenticators = new byte[(3 * F + 1) * 8 + 8];
		System.arraycopy(authenticators, 0, buffer, bufferIndex,
				authenticators.length);
		bufferIndex += authenticators.length;
		// prettyPrintHex(buffer);
		// Send it.
		// /////////
		DatagramPacket sendp = new DatagramPacket(buffer, bufferIndex);
//				InetAddress.getLocalHost(), port);

		try {
			socket.send(sendp);
			System.out.println("Sent " + sendp.getLength()
					+ " worth of data");
		} catch (IOException e) {
			throw new Exception("write(): " + e.toString());
		} catch (Exception e) {
			System.out.println("Caught exception " + e.getMessage());
			System.out.println("Buffer size " + buffer.length + " index "
					+ bufferIndex + " address " + address);
			e.printStackTrace();
		}

	}

	public void pbft_send(int cid, Serializable msg) throws Exception {

		byte[] messageInBytes = null;
		try {
			ByteArrayOutputStream baos = new ByteArrayOutputStream();
			ObjectOutputStream oos = new ObjectOutputStream(baos);
			oos.writeObject(msg);
			messageInBytes = baos.toByteArray();
			prettyPrintHex(messageInBytes);
			oos.close();
		} catch (IOException e) {
		}
		pbft_send(cid, messageInBytes);
	}
	
	public void pbft_send(int cid, byte[] messageInBytes) throws Exception 
	{
		// since the message can be >4096 which is the pbft's limit
		// we must somehow split these messages
		int len = messageInBytes.length;
		int position = 0;
		// we encode the client ID, and sequence number into the payload
		byte[] payload;// = new byte[MAX_PAYLOAD_LENGTH];
		if (len < MAX_PAYLOAD_LENGTH - 5) {
			payload = new byte[len+5];
			toBytes(cid, payload, 0);
			payload[4] = 0;
			System.arraycopy(messageInBytes, 0, payload, 5, len);
			
			try {
				send(cid, payload);
			} catch (Exception e) {
				return;
			}
		}
		// now, lets assume there are no dropped messages from hedera to pbft...
		/*
		 * else if (len > 254 * (PBFTMessage.MAX_PAYLOAD_LENGTH-5)) { // we
		 * can't handle long messages return null; }
		 */
		// so, there is no need to place counters
		// 0xaa means there are still bytes
		// 0xff means it is the last message
		else {
			// byte counter = 1;
			payload = new byte[MAX_PAYLOAD_LENGTH];
			do {
				toBytes(cid, payload, 0);
				payload[4] = (len > MAX_PAYLOAD_LENGTH - 5) ? (byte) 0xaa : (byte) 0xff;
				int amount = (len > MAX_PAYLOAD_LENGTH - 5) ? MAX_PAYLOAD_LENGTH - 5 : len;
				System.arraycopy(messageInBytes, position, payload, 5, amount);
				try {
					send(cid, payload);
				} catch (Exception e) {
					return;
				}
				position += amount;
				len -= amount;
			} while (len > 0);
		}
	}

	// Actually receives a new message
	// /////////////////////////////////
	private byte[] receive() throws Exception, InterruptedIOException {
		// Read the message.
		// /////////////////
		byte message[] = new byte[MAX_MESSAGE_LENGTH];
		DatagramPacket recvp = new DatagramPacket(message, MAX_MESSAGE_LENGTH);
		System.out.println("Listening on port " + socket.getLocalPort());
		try {
			socket.receive(recvp);
		} catch (InterruptedIOException e) {
			throw e;
		} catch (IOException e) {
			throw new Exception("read(): " + e);
		}
		System.out.println("Got message of length " + recvp.getLength());
		// message is just the text we sent originally

		// Read in the data.
		// //////////////////
		byte data[] = new byte[recvp.getLength()];
		try {
			System.arraycopy(message, 0, data, 0, recvp.getLength());
		} catch (IndexOutOfBoundsException e) {
			throw new Exception("Index out of bounds" + e);
		} catch (ArrayStoreException e) {
			throw new Exception(e);
		}

		return data;
	}

	/**
	 * Receives a message from the pbft.
	 * 
	 * @see org.continuent.hedera.channel.AbstractReliableGroupChannel#receive()
	 * @return received message
	 * @throws ChannelException
	 *             failed when receiving message
	 * @throws NotConnectedException
	 *             if the channel is not connected
	 */
	public Object[] pbft_receive() throws Exception {
		//while (true) {
			byte[] payload;
			try {
				payload = receive();
			} catch (InterruptedIOException e1) {
				throw new Exception(e1);
			}

			Serializable message = null;
			int cid;
			if (payload[4] == 0) {
				prettyPrintHex(payload);
				cid = toInt(payload, 0);
				byte[] msgdat = new byte[payload.length - 5];
				System.arraycopy(payload, 5, msgdat, 0, payload.length - 5);

				try {
					ByteArrayInputStream bais = new ByteArrayInputStream(msgdat);
					ObjectInputStream ois = new ObjectInputStream(bais);
					message = (Serializable) ois.readObject();
					ois.close();
				} catch (StreamCorruptedException e) {
					// will use the rest as a string
					message = new String(msgdat);
				} catch (IOException e) {
					System.out.println("Exception: "+e);
					message = null;
				} catch (ClassNotFoundException e) {
					System.out.println("Exception: "+e);
					message = null;
				}
				System.out.println("Message is "+message+"\ncid is "+cid);
				List<byte[]> ll = receivedMessages[cid];
				ll.clear();
				Object o[] = {cid, message};
				return o;
			} else if (payload[4] == 0xff) {
				// EOM received, serialize and return
				cid = toInt(payload, 0);
				List<byte[]> ll = receivedMessages[cid];
				// join all elements of a linked list
				ll.add(payload);
				// serialize
				int finalSize = ll.size() * (MAX_PAYLOAD_LENGTH - 5);
				byte[] data = new byte[finalSize];
				int position = 0;
				ListIterator<byte[]> iterator = ll.listIterator();
				while (iterator.hasNext()) {
					byte[] pdata = (byte[]) iterator.next();
					System.arraycopy(pdata, 5, data, position, pdata.length - 5);
					position += pdata.length - 5;
				}
				try {
					ByteArrayInputStream bais = new ByteArrayInputStream(data);
					ObjectInputStream ois = new ObjectInputStream(bais);
					message = (Serializable) ois.readObject();
					ois.close();
				} catch (IOException e) {
					message = null;
				} catch (ClassNotFoundException e) {
					message = null;
				}
				ll.clear();
				Object[] o = {cid, message};
				return o;
			} else if (payload[4] == 0xaa) {
				// just append
				cid = toInt(payload, 0);
				// insert into some linked list
				List<byte[]> ll = receivedMessages[cid];
				ll.add(payload);
				return null;
			}
		//}
			return null;
	}
}
