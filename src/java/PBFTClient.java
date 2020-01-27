import java.net.*;
import java.io.*;
import java.util.*;
import java.sql.*;
import javax.sql.rowset.*;
import com.sun.rowset.*;
import java.security.*;
import java.math.*;

public class PBFTClient
{
	private static ResourceBundle configuration = null;
	  
    private PBFTConnection conn;
    
    private Boolean wsynchro[];
    
    // used to synchronize writers and PBFT writer, so they do not use the socket at the same time
    // client == 1 means there is a request sent
    private int numClients;
    private Integer[] clients;
    private int[] ids;
    private Map<Integer, Integer> reverseIds;
    
    private PBFTWriter[] pbftWriters;
    private Writer[] writers;
    private Listener[] listeners;
    
    private Map<Integer, Socket> servers;
    
    private int f;
    
    /**
     * Returns the value corresponding to a property in the .properties file.
     *
     * @param property the property name
     * @return a <code>String</code> value
     */
    protected String getProperty(String property)
    {
    	try {
    		String s = configuration.getString(property);
    		return s;
    	} catch (MissingResourceException e) {
    		return null;
    	}
    }
    
    public PBFTClient(String filename)
    {
	    // Get and check database.properties
	    System.out.println("Looking for "+filename+".properties in classpath ("+System.getProperty("java.class.path",".")+")");
	    try
	    {
		    configuration = ResourceBundle.getBundle(filename);
	    }
	    catch (java.util.MissingResourceException e)
	    {
		    System.err.println("No "+filename+".properties file has been found in your classpath.<p>");
		    Runtime.getRuntime().exit(1);
	    }

	    int maxClients = new Integer(getProperty("client.max_clients"));
	    conn = new PBFTConnection(maxClients);
	    int port = 0;
	    try {
		    port = new Integer(getProperty("server.pbft.port"));
	    } catch (Exception e) {
		    port = 0;
	    }
	    InetAddress addr;
	    try {
		    addr = InetAddress.getByName(getProperty("server.pbft.addr"));
	    } catch (UnknownHostException e1) {
		    addr = null;
		    System.out.println("Couldn't get the address of " + getProperty("server.pbft.addr"));
	    }

	    try {
		    conn.connect(addr, port);
	    } catch (Exception e) {
		    System.out.println("Couldn't connect to the pbft server");
		    e.printStackTrace();
		    return;
	    }

	    // setup connections to the servers
	    // we'll use these connections for receiving sql results
	    // or for sending read only requests
	    int numServers = new Integer(getProperty("server.num_servers"));
	    f = new Integer(getProperty("f"));
	    listeners = new Listener[numServers];
	    writers = new Writer[numServers];

	    // initialize synchronization data
	    StringTokenizer tids = new StringTokenizer(getProperty("client.my_ids"),",");
	    numClients = tids.countTokens();
	    clients = new Integer[numClients];
	    ids = new int[numClients];
	    reverseIds = new HashMap<Integer,Integer>();
	    wsynchro = new Boolean[numClients];

	    int count = 0;
	    while (tids.hasMoreTokens())
	    {
		    clients[count] = 0;
		    ids[count] = new Integer(tids.nextToken());
		    reverseIds.put(ids[count], count);
		    wsynchro[count] = new Boolean(false);
		    count++;
	    }

	    servers = new HashMap<Integer, Socket>();
	    for (int i=0; i < numServers; i++) {
		    Socket serverSock = null;
		    String serverName = getProperty("server."+i+".java.addr");
		    int serverPort = new Integer(getProperty("server."+i+".java.port"));
		    try {
			    InetSocketAddress sockaddr = new InetSocketAddress(serverName, serverPort);
			    serverSock = new Socket();
			    serverSock.connect(sockaddr, 10000);
			    System.out.println("Connected to " + sockaddr);
		    } catch (IOException e) {
			    System.out.println("Could not connect to server "+serverName+"\n"+e);
			    serverSock = null;
		    }

		    servers.put(i, serverSock);

		    // send id to the server, so it can store at the right place
		    try {
			    // Send a message to the client application
			    ObjectOutputStream oos = new ObjectOutputStream(serverSock.getOutputStream());
			    oos.writeObject(ids[i]);
			    oos.flush();
			    //oos.close();
			    System.out.println("Sent identification ("+ids[i]+") to the server...");
		    } catch (UnknownHostException e) {
			    e.printStackTrace();
		    } catch (IOException e) {
			    e.printStackTrace();
		    }

		    // now, lets start listeners and writers
		    listeners[i] = new Listener(i, serverSock);
		    listeners[i].start();
		    writers[i] = new Writer(i, serverSock);
		    writers[i].start();
	    }

	    // now is the time to start the writers
	    pbftWriters = new PBFTWriter[count];
	    for (int i = 0; i < count; i++) 
	    {
		    pbftWriters[i] = new PBFTWriter(conn, ids[i], i);
	    }

	    for (int i=0; i < ids.length; i++) {
		    pbftWriters[i].start();
	    }
	    /*
	       this.cid = cid;
	       this.maxClientId = maxCid;

	       receivedMessages = new List[maxClientId];
	       for (int i=0; i<receivedMessages.length; i++) {
	       receivedMessages[i] = new LinkedList<byte[]>(); 
	       receivedMessages[i] = Collections.synchronizedList(receivedMessages[i]);
	       }
	     */
    }

    // this command should return a sql command
    private static byte[] generateSQL(int mode)
    {
    	final Random gen = new Random();
    	// mode == 0 is a write
    	if (mode == 0) {
			int id = gen.nextInt(40000);
			int uid = gen.nextInt(10000);
    		switch (gen.nextInt(5)) {
    		case 0:
    			return new String("update bids set qty=qty+1 where id = " + id).getBytes();
    		case 1:
    			return new String("update bids set qty=qty-1 where id = " + id).getBytes();
    		case 2:
    			return new String("delete from bids where qty=0").getBytes();
    		case 3:
    			return new String("update users set rating = "+(100-gen.nextInt(200))+" where id =" + uid).getBytes();
    		case 4:
    			return new String("update users set password = \"password" + id +"\" where id = "+uid).getBytes();
    		}
    	} else {
    		// read command
			int id = gen.nextInt(40000);
			int uid = gen.nextInt(10000);
    		switch (gen.nextInt(5)) {
    		case 0:
    			return new String("select count(*) from bids").getBytes();
    		case 1:
    			return new String("select count(*) from users").getBytes();
    		case 2:
    			return new String("select * from users where id = "+uid).getBytes();
    		case 3:
    			return new String("select * from bids where user_id = "+uid).getBytes();
    		case 4:
    			return new String("select * from items where id = "+id).getBytes();
    		}
    	}
    	return "select 1".getBytes();
    }

    public static final void main(String[] args)
    {
    	String propertiesFileName;
        if (args.length == 0)
            propertiesFileName = "pbft_client";
          else
            propertiesFileName = args[0];
        
        PBFTClient client = new PBFTClient(propertiesFileName);
    }
    
    private class PBFTWriter extends Thread
    {
    	private PBFTConnection connection;
    	protected boolean signal;
    	protected int mycid;
    	protected int pos;
    	
    	// there is one thread for each client, and pos keeps the position in array clients
    	// if there is 0 in that array, at pos, that means client got response/can send
    	public PBFTWriter(PBFTConnection con, int cid, int pos) {
    		signal = false;
    		connection = con;
    		this.mycid = cid;
    		this.pos = pos;
    		clients[pos] = 0;
    		// this.setDaemon(true);
    	}
    	
    	public void run()
    	{
    		System.out.println("PBFTWriter: thread "+mycid+" running");
    		while (true)
    		{
    			// all of them shouldn't use the connection at the same time
    			synchronized(connection)
    			{
    				if (signal == true) {
    					return;
    				}
    				// send a message somehow
    				try
    				{
    					// for a client, if free...
    					if (clients[pos] != 0) 
    						continue;

    					// invent a command, write only
    					byte[] msg = PBFTClient.generateSQL(0);

    					System.out.println("PBFTWriter("+mycid+"): will send a message, len "+msg.length+"...");
					System.out.println("\tmessage: \n"+new String(msg)+"\teof");
    					synchronized(wsynchro[pos]) {
    						connection.pbft_send(mycid, msg);
    						clients[pos] = 1;
    					}
    					System.out.println("PBFTWriter("+mycid+"): \tsent.");

    				}
    				catch (Exception e)
    				{
    					System.out.println("PBFTWriter:run caught exception "+e);
    					// do something
    				}
    			}
    			try {
    				// time to sleep between requests
					sleep(400);
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
    		}
    	}
    	
    }

    private class Listener extends Thread
    {
    	protected boolean signal;
    	protected int sid;
    	private Socket socket;
    	private InputStream istream;

    	// there is one thread for each client, and pos keeps the position in array clients
    	// if there is 0 in that array, at pos, that means client got response/can send
    	public Listener(int sid, Socket socket) {
    		signal = false;
    		this.sid = sid;
    		this.socket = socket;
		try {
			this.istream = socket.getInputStream();
		} catch (IOException e) {
			System.out.println("Can't get input stream for socket of server "+sid+"\n"+e);
		}
    		this.setDaemon(true);
    	}

    	// NOTE: client expects first to get id, and the the result of the command
    	public void run()
    	{
		System.out.println("Starting Listener for server "+sid);
    		while (true)
    		{
    			if (signal == true) {
    				return;
    			}
    			// send a message somehow
    			try
    			{
    				synchronized(socket)
    				{
    					ObjectInputStream ois = new ObjectInputStream(istream);
    					Integer cid = (Integer) ois.readObject();
    					Serializable response = (Serializable) ois.readObject();
    					clients[reverseIds.get(cid)] = 0;
    					System.out.println("Received response from server "+sid+", for client "+cid+" of type "+response.getClass());
    				}
    			}
    			catch (Exception e)
    			{
				System.out.println("Got exception while waiting for data from server "+sid);
				e.printStackTrace();
				return;
    				// do something
    			}
    		}
    	}
    }
    
    private class Writer extends Thread
    {
    	protected boolean signal;
    	protected int sid;
    	private Socket socket;
    	private OutputStream ostream;
    	private Random generator;

    	// there is one thread for each client, and pos keeps the position in array clients
    	// if there is 0 in that array, at pos, that means client got response/can send
    	public Writer(int sid, Socket socket) {
    		signal = false;
    		this.sid = sid;
    		this.socket = socket;
		try {
			this.ostream = socket.getOutputStream();
		} catch (IOException e) {
			System.out.println("Can't get output stream for socket of server "+sid);
		}
		generator = new Random();
		//this.setDaemon(true);
    	}

    	// NOTE: client expects first to get id, and the the result of the command
    	public void run()
    	{
		System.out.println("Writer for server "+sid+" started.");
    		while (true)
    		{
    			if (signal == true) {
    				return;
    			}
    			// send a message:
    			// find a client to emulate
    			// check if not waiting for a message
    			// generate sql
    			// send
    			Integer pos = generator.nextInt(numClients);
    			synchronized(clients[pos]) {
    				if (clients[pos] == 1)
    					continue;
    				try
    				{
    					synchronized(socket)
    					{
    						ObjectOutputStream ois = new ObjectOutputStream(ostream);
    						ois.writeObject(ids[pos]);
    						ois.writeObject(new String(PBFTClient.generateSQL(1)));
    						clients[pos] = 1;
						System.out.println("Writer["+sid+"]: Will write on behalf of client "+ids[pos]);
    					}
    				}
    				catch (Exception e)
    				{
					System.out.println("Caught exception while writing to socket for server "+e);
					e.printStackTrace();
					return;
    					// do something
    				}
    			}
    		}
    	}
    }  
}

